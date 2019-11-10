/**
 * @file MathBot2001.cpp
 *
 * This module contains the implementation of the MathBot2001 class.
 *
 * © 2018 by Richard Walters
 */

#include "MathBot2001.hpp"
#include "TimeKeeper.hpp"

#include <condition_variable>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <StringExtensions/StringExtensions.hpp>
#include <SystemAbstractions/DiagnosticsSender.hpp>
#include <SystemAbstractions/File.hpp>
#include <thread>
#include <Twitch/Messaging.hpp>
#include <TwitchNetworkTransport/Connection.hpp>

namespace {

    /**
     * This is the number of milliseconds to wait between rounds of polling
     * in the worker thread of the chat room.
     */
    constexpr unsigned int WORKER_POLLING_PERIOD_MILLISECONDS = 50;

    /**
     * This is the nickname the bot should use.
     */
    const std::string BOT_NICKNAME = "MathBot2001";

    /**
     * This represents one user who is interacting with the bot.
     */
    struct Contestant {
        /**
         * This is the user's nickname.
         */
        std::string nickname;

        /**
         * This is the user's current score.
         */
        int points = 0;

        /**
         * This is the number of points gained or lost this round.
         */
        int pointDelta = 0;
    };

}

/**
 * This contains the private properties of a MathBot2001 class instance.
 */
struct MathBot2001::Impl
    : public Twitch::Messaging::User
{
    // Properties

    /**
     * This is a helper object used to generate and publish
     * diagnostic messages.
     */
    SystemAbstractions::DiagnosticsSender diagnosticsSender;

    /**
     * This is the name of the channel to join in Twitch.
     */
    std::string channel;

    /**
     * This is used to connect to Twitch chat and exchange messages
     * with it.
     */
    Twitch::Messaging tmi;

    /**
     * This is used to track elapsed real time.
     */
    std::shared_ptr< TimeKeeper > timeKeeper = std::make_shared< TimeKeeper >();

    /**
     * This is used to synchronize access to the object.
     */
    std::mutex mutex;

    /**
     * This is used to signal when any condition for which the main thread
     * may be waiting has occurred.
     */
    std::condition_variable mainThreadEvent;

    /**
     * This flag is set when the Twitch messaging interface indicates
     * that the bot has been logged out of Twitch.
     */
    bool loggedOut = false;

    /**
     * This is used to notify the worker thread about
     * any change that should cause it to wake up.
     */
    std::condition_variable_any workerWakeCondition;

    /**
     * This is used to have the bot
     * take action at certain points in time.
     */
    std::thread workerThread;

    /**
     * This flag indicates whether or not the worker thread should stop.
     */
    bool stopWorker = false;

    /**
     * This is used to generate the math questions.
     */
    std::mt19937 generator;

    /**
     * This indicates whether or not a user has sent a tell
     * with the correct answer to the current math question,
     * or if the round has finished before anyone could answer
     * the question correctly.
     */
    bool roundComplete = true;

    /**
     * This indicates whether or not the current round has been scored.
     */
    bool roundScored = true;

    /**
     * This is the time (according to the time keeper) when
     * the next math question should be asked.
     */
    double nextQuestionTime = std::numeric_limits< double >::max();

    /**
     * This is the time (according to the time keeper) when
     * the current math question should be scored.
     */
    double currentScoringTime = std::numeric_limits< double >::max();

    /**
     * This is the minimum cooldown time in seconds between
     * when two consecutive questions are asked.
     */
    double minQuestionCooldown = 45.0;

    /**
     * This is the maximum cooldown time in seconds between
     * when two consecutive questions are asked.
     */
    double maxQuestionCooldown = 180.0;

    /**
     * This is the amount of time a question/answer round will go
     * until the scoring is done.
     */
    double roundTime = 15.0;

    /**
     * This is the correct answer to the current math question.
     */
    std::string answer;

    /**
     * These are the users who are currently interacting with the bot.
     */
    std::map< std::string, Contestant > contestants;

    /**
     * These are the nicknames of the users who participated in answering
     * the last question.
     */
    std::set< std::string > nicknamesOfParticipantsThisRound;

    /**
     * This is the nickname of the user who won the last round.
     */
    std::string winnerThisRound;

    // Methods

    /**
     * This is the default constructor.
     */
    Impl()
        : diagnosticsSender("MathBot2001")
    {
    }

    /**
     * This method updates the times of when the current question
     * will be scored, and the next question asked.
     */
    void UpdateRoundTimes() {
        currentScoringTime = nextQuestionTime + roundTime;
        nextQuestionTime += std::uniform_real_distribution<>(
            minQuestionCooldown,
            maxQuestionCooldown
        )(generator);
    }

   /**
     * This is method starts the worker thread if it isn't running.
     */
    void StartWorker() {
        if (workerThread.joinable()) {
            return;
        }
        stopWorker = false;
        generator.seed((int)time(NULL));
        nextQuestionTime = timeKeeper->GetCurrentTime();
        workerThread = std::thread(&Impl::Worker, this);
    }

    /**
     * This method stops the worker thread if it's running.
     */
    void StopWorker() {
        if (!workerThread.joinable()) {
            return;
        }
        {
            std::lock_guard< decltype(mutex) > lock(mutex);
            stopWorker = true;
            workerWakeCondition.notify_all();
        }
        workerThread.join();
    }

    /**
     * This method clears any information about the last round,
     * and starts a new question/answer round.
     *
     * @return
     *     The next question is returned.
     */
    std::string StartNewRound() {
        const auto lastAnswer = answer;
        nicknamesOfParticipantsThisRound.clear();
        winnerThisRound.clear();
        std::string question;
        do {
            std::vector< int > questionComponents(3);
            questionComponents[0] = std::uniform_int_distribution<>(2, 10)(generator);
            questionComponents[1] = std::uniform_int_distribution<>(2, 10)(generator);
            questionComponents[2] = std::uniform_int_distribution<>(2, 97)(generator);
            question = StringExtensions::sprintf(
                "What is %d * %d + %d?",
                questionComponents[0],
                questionComponents[1],
                questionComponents[2]
            );
            answer = StringExtensions::sprintf(
                "%d",
                questionComponents[0] * questionComponents[1] + questionComponents[2]
            );
        } while (answer == lastAnswer);
        roundScored = false;
        roundComplete = false;
        UpdateRoundTimes();
        return question;
    }

    /**
     * This method updates the scores of all users who participated
     * this round, and returns a string which describes who lost,
     * which is intended to be included in the results
     * message sent to the channel.
     *
     * @return
     *     A string which describes who lost,
     *     which is intended to be included in the results
     *     message sent to the channel, is returned.
     */
    std::string ApplyScoresAndGetLosers() {
        std::ostringstream buffer;
        bool firstLoser = true;
        for (const auto& nickname: nicknamesOfParticipantsThisRound) {
            contestants[nickname].points += contestants[nickname].pointDelta;
            if (nickname != winnerThisRound) {
                if (firstLoser) {
                    firstLoser = false;
                } else {
                    buffer << ", ";
                }
                buffer
                    << nickname << " ("
                    << contestants[nickname].pointDelta << " -> "
                    << contestants[nickname].points << ")";
            }
        }
        return buffer.str();
    }

    /**
     * This function is called in a separate thread to have the bot
     * take action at certain points in time.
     */
    void Worker() {
        std::unique_lock< decltype(mutex) > lock(mutex);
        while (!stopWorker) {
            workerWakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(WORKER_POLLING_PERIOD_MILLISECONDS),
                [this]{ return stopWorker; }
            );
            const auto now = timeKeeper->GetCurrentTime();
            if (now >= nextQuestionTime) {
                const auto question = StartNewRound();
                lock.unlock();
                tmi.SendMessage(channel, question);
                lock.lock();
            } else if (
                (now >= currentScoringTime)
                && !roundScored
            ) {
                roundComplete = true;
                roundScored = true;
                const auto losersList = ApplyScoresAndGetLosers();
                std::ostringstream buffer;
                if (winnerThisRound.empty()) {
                    buffer << "No winners this round";
                    if (!losersList.empty()) {
                        buffer << ", only losers BibleThump " << losersList;
                    }
                } else {
                    buffer
                        << "Congratulations, " << winnerThisRound << "! (now at "
                        << contestants[winnerThisRound].points << " point"
                        << ((contestants[winnerThisRound].points == 1) ? "" : "s")
                        << ")";
                    if (!losersList.empty()) {
                        buffer << " FeelsBadMan " << losersList;
                    }
                }
                buffer << ".";
                tmi.SendMessage(
                    channel,
                    buffer.str()
                );
            }
        }
    }

    /**
     * This method is called to check if a tell sent by a user
     * appears to be an attempt to answer the last question.  If it is,
     * the answer is checked for accuracy, and the user is either awarded
     * a point or penalized a point.
     *
     * @param[in] userNickname
     *     This is the nickname of the user who sent the tell.
     *
     * @param[in] tell
     *     This is the content of the user's tell.
     *
     * @note
     *     If the last question was already answered correctly, any
     *     subsequent answers are ignored, until the next question is asked.
     */
    void IfMessageIsAnswerThenHandleIt(
        const std::string& userNickname,
        const std::string& tell
    ) {
        intmax_t tellAsNumber;
        if (
            StringExtensions::ToInteger(tell, tellAsNumber)
            != StringExtensions::ToIntegerResult::Success
        ) {
            return;
        }
        if (roundComplete) {
            return;
        }
        auto& userEntry = contestants[userNickname];
        if (nicknamesOfParticipantsThisRound.insert(userNickname).second) {
            userEntry.pointDelta = 0;
        }
        userEntry.nickname = userNickname;
        if (tell == answer) {
            diagnosticsSender.SendDiagnosticInformationString(1, "Winner: " + userNickname);
            winnerThisRound = userNickname;
            roundComplete = true;
            ++userEntry.pointDelta;
        } else {
            diagnosticsSender.SendDiagnosticInformationString(1, "Loser: " + userNickname);
            --userEntry.pointDelta;
        }
    }

    // Twitch::Messaging::User

    virtual void LogIn() override {
        diagnosticsSender.SendDiagnosticInformationString(1, "Logged in.");
        tmi.Join(channel);
    }

    virtual void LogOut() override {
        if (loggedOut) {
            return;
        }
        StopWorker();
        diagnosticsSender.SendDiagnosticInformationString(1, "Logged out.");
        std::lock_guard< decltype(mutex) > lock(mutex);
        loggedOut = true;
        mainThreadEvent.notify_one();
    }

    virtual void Join(
        Twitch::Messaging::MembershipInfo&& membershipInfo
    ) override {
        if (membershipInfo.user == StringExtensions::ToLower(BOT_NICKNAME)) {
            StartWorker();
        }
    }

    virtual void Leave(
        Twitch::Messaging::MembershipInfo&& membershipInfo
    ) override {
        if (membershipInfo.user == StringExtensions::ToLower(BOT_NICKNAME)) {
            StopWorker();
        }
    }

    virtual void Message(
        Twitch::Messaging::MessageInfo&& messageInfo
    ) override {
        diagnosticsSender.SendDiagnosticInformationFormatted(
            1, "%s said in channel \"%s\", \"%s\"",
            messageInfo.user.c_str(),
            messageInfo.channel.c_str(),
            messageInfo.messageContent.c_str()
        );
        IfMessageIsAnswerThenHandleIt(
            messageInfo.user,
            messageInfo.messageContent
        );
    }

};

MathBot2001::~MathBot2001() noexcept = default;

MathBot2001::MathBot2001()
    : impl_(new Impl())
{
}

void MathBot2001::Configure(
    SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate diagnosticMessageDelegate
) {
    impl_->diagnosticsSender.SubscribeToDiagnostics(diagnosticMessageDelegate, 0);
    impl_->tmi.SubscribeToDiagnostics(impl_->diagnosticsSender.Chain(), 0);
    impl_->tmi.SetConnectionFactory(
        [diagnosticMessageDelegate]() -> std::shared_ptr< Twitch::Connection > {
            auto connection = std::make_shared< TwitchNetworkTransport::Connection >();
            connection->SubscribeToDiagnostics(diagnosticMessageDelegate, 0);
            SystemAbstractions::File caCertsFile(
                SystemAbstractions::File::GetExeParentDirectory()
                + "/cert.pem"
            );
            if (!caCertsFile.Open()) {
                diagnosticMessageDelegate(
                    "MathBot2001",
                    SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                    StringExtensions::sprintf(
                        "unable to open root CA certificates file '%s'",
                        caCertsFile.GetPath().c_str()
                    )
                );
                return nullptr;
            }
            std::vector< uint8_t > caCertsBuffer(caCertsFile.GetSize());
            if (caCertsFile.Read(caCertsBuffer) != caCertsBuffer.size()) {
                diagnosticMessageDelegate(
                    "MathBot2001",
                    SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                    "unable to read root CA certificates file"
                );
                return nullptr;
            }
            const std::string caCerts(
                (const char*)caCertsBuffer.data(),
                caCertsBuffer.size()
            );
            connection->SetCaCerts(caCerts);
            return connection;
        }
    );
    impl_->tmi.SetTimeKeeper(impl_->timeKeeper);
    impl_->tmi.SetUser(
        std::shared_ptr< Twitch::Messaging::User >(
            impl_.get(),
            [](Twitch::Messaging::User*){}
        )
    );
    impl_->diagnosticsSender.SendDiagnosticInformationString(3, "Configured.");
}

void MathBot2001::InitiateLogIn(
    const std::string& token,
    const std::string& channel
) {
    impl_->channel = channel;
    impl_->tmi.LogIn(BOT_NICKNAME, token);
}

void MathBot2001::InitiateLogOut() {
    impl_->diagnosticsSender.SendDiagnosticInformationString(3, "Exiting...");
    impl_->tmi.LogOut("Bye! BibleThump");
}

bool MathBot2001::AwaitLogOut() {
    std::unique_lock< decltype(impl_->mutex) > lock(impl_->mutex);
    return impl_->mainThreadEvent.wait_for(
        lock,
        std::chrono::milliseconds(250),
        [this]{ return impl_->loggedOut; }
    );
}
