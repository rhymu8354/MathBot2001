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
#include <string>
#include <SystemAbstractions/DiagnosticsSender.hpp>
#include <SystemAbstractions/File.hpp>
#include <SystemAbstractions/StringExtensions.hpp>
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
     * This is the Twitch channel to join in chat.
     *
     * @note
     *     You'll need to set this to some valid channel in order for
     *     this bot to work.
     */
    const std::string TWITCH_CHANNEL = "";

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
     * This is the OAuth token to use in authenticating with Twitch.
     */
    std::string token;

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
     * with the correct answer to the current math question.
     */
    bool answeredCorrectly = true;

    /**
     * This is the time (according to the time keeper) when
     * the next math question should be asked.
     */
    double nextQuestionTime = std::numeric_limits< double >::max();

    /**
     * This is the minimum cooldown time in seconds between
     * when two consecutive questions are asked.
     */
    double minQuestionCooldown = 10.0;

    /**
     * This is the maximum cooldown time in seconds between
     * when two consecutive questions are asked.
     */
    double maxQuestionCooldown = 30.0;

    /**
     * This is the correct answer to the current math question.
     */
    std::string answer;

    /**
     * These are the users who are currently interacting with the bot.
     */
    std::map< std::string, Contestant > contestants;

    // Methods

    /**
     * This is the default constructor.
     */
    Impl()
        : diagnosticsSender("MathBot2001")
    {
    }

    /**
     * This method sets the time the next math question will be asked.
     */
    void CooldownNextQuestion() {
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
                const auto lastAnswer = answer;
                std::string question;
                do {
                    std::vector< int > questionComponents(3);
                    questionComponents[0] = std::uniform_int_distribution<>(2, 10)(generator);
                    questionComponents[1] = std::uniform_int_distribution<>(2, 10)(generator);
                    questionComponents[2] = std::uniform_int_distribution<>(2, 97)(generator);
                    question = SystemAbstractions::sprintf(
                        "What is %d * %d + %d?",
                        questionComponents[0],
                        questionComponents[1],
                        questionComponents[2]
                    );
                    answer = SystemAbstractions::sprintf(
                        "%d",
                        questionComponents[0] * questionComponents[1] + questionComponents[2]
                    );
                } while (answer == lastAnswer);
                answeredCorrectly = false;
                CooldownNextQuestion();
                lock.unlock();
                tmi.SendMessage(TWITCH_CHANNEL, question);
                lock.lock();
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
            SystemAbstractions::ToInteger(tell, tellAsNumber)
            != SystemAbstractions::ToIntegerResult::Success
        ) {
            return;
        }
        auto& userEntry = contestants[userNickname];
        userEntry.nickname = userNickname;
        if (answeredCorrectly) {
            return;
        }
        if (tell == answer) {
            answeredCorrectly = true;
            ++userEntry.points;
            tmi.SendMessage(
                TWITCH_CHANNEL,
                SystemAbstractions::sprintf(
                    "Congratulations, %s!  That's the correct answer.  You now have %d points.",
                    userEntry.nickname.c_str(),
                    userEntry.points
                )
            );
        } else {
            --userEntry.points;
            tmi.SendMessage(
                TWITCH_CHANNEL,
                SystemAbstractions::sprintf(
                    "Sorry, %s, that isn't the correct answer.  You now have %d points.",
                    userEntry.nickname.c_str(),
                    userEntry.points
                )
            );
        }
    }

    // Twitch::Messaging::User

    virtual void LogIn() override {
        diagnosticsSender.SendDiagnosticInformationString(1, "Logged in.");
        tmi.Join(TWITCH_CHANNEL);
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
        const std::string& channel,
        const std::string& user
    ) override {
        if (user == SystemAbstractions::ToLower(BOT_NICKNAME)) {
            StartWorker();
        }
    }

    virtual void Leave(
        const std::string& channel,
        const std::string& user
    ) {
        if (user == SystemAbstractions::ToLower(BOT_NICKNAME)) {
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
            messageInfo.message.c_str()
        );
        IfMessageIsAnswerThenHandleIt(
            messageInfo.user,
            messageInfo.message
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
                    SystemAbstractions::sprintf(
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

void MathBot2001::InitiateLogIn(const std::string& token) {
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
