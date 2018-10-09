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
#include <mutex>
#include <string>
#include <SystemAbstractions/DiagnosticsSender.hpp>
#include <SystemAbstractions/File.hpp>
#include <SystemAbstractions/StringExtensions.hpp>
#include <Twitch/Messaging.hpp>
#include <TwitchNetworkTransport/Connection.hpp>

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

    // Methods

    /**
     * This is the default constructor.
     */
    Impl()
        : diagnosticsSender("MathBot2001")
    {
    }

    // Twitch::Messaging::User

    virtual void LogIn() override {
        diagnosticsSender.SendDiagnosticInformationString(1, "Logged in.");
        tmi.Join("rhymu8354");
    }

    virtual void LogOut() override {
        if (loggedOut) {
            return;
        }
        diagnosticsSender.SendDiagnosticInformationString(1, "Logged out.");
        std::lock_guard< decltype(mutex) > lock(mutex);
        loggedOut = true;
        mainThreadEvent.notify_one();
    }

    virtual void Join(
        const std::string& channel,
        const std::string& user
    ) override {
        if (user == "mathbot2001") {
            tmi.SendMessage(channel, "Hello, I'm a bot!");
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
    const auto timeKeeper = std::make_shared< TimeKeeper >();
    impl_->tmi.SetTimeKeeper(std::make_shared< TimeKeeper >());
    impl_->tmi.SetUser(
        std::shared_ptr< Twitch::Messaging::User >(
            impl_.get(),
            [](Twitch::Messaging::User*){}
        )
    );
    impl_->diagnosticsSender.SendDiagnosticInformationString(3, "Configured.");
}

void MathBot2001::InitiateLogIn(const std::string& token) {
    impl_->tmi.LogIn("MathBot2001", token);
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
