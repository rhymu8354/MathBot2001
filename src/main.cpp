/**
 * @file main.cpp
 *
 * This module holds the main() function, which is the entrypoint
 * to the program.
 *
 * © 2018 by Richard Walters
 */

#include "TimeKeeper.hpp"

#include <condition_variable>
#include <mutex>
#include <Twitch/Messaging.hpp>
#include <TwitchNetworkTransport/Connection.hpp>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <SystemAbstractions/DiagnosticsStreamReporter.hpp>
#include <SystemAbstractions/File.hpp>
#include <SystemAbstractions/StringExtensions.hpp>
#include <thread>

namespace {

    /**
     * This function prints to the standard error stream information
     * about how to use this program.
     */
    void PrintUsageInformation() {
        fprintf(
            stderr,
            (
                "Usage: MathBot2001\n"
                "\n"
                "Connect to Twitch chat and listen for messages.\n"
                "\n"
                "  TOKEN   Path/name of file containing the OAuth token to use\n"
            )
        );
    }

    /**
     * This flag indicates whether or not the web client should shut down.
     */
    bool shutDown = false;

    /**
     * This contains variables set through the operating system environment
     * or the command-line arguments.
     */
    struct Environment {
        /**
         * This is the OAuth token to use in authenticating with Twitch.
         */
        std::string token;
    };

    /**
     * This represents the chat bot itself.  It handles any callbacks
     * received from the Twitch messaging interface.
     */
    struct MathBot2001
        : public Twitch::Messaging::User
    {
        // Properties

        /**
         * This is used to report information to the bot's operator.
         */
        SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate diagnosticMessageDelegate;

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
         * This method waits up to a quarter second for the bot to be
         * logged out of Twitch.
         *
         * @return
         *     An indication of whether or not the bot has been logged
         *     out of Twitch is returned.
         */
        bool AwaitLogOut() {
            std::unique_lock< decltype(mutex) > lock(mutex);
            return mainThreadEvent.wait_for(
                lock,
                std::chrono::milliseconds(250),
                [this]{ return loggedOut; }
            );
        }

        // Twitch::Messaging::User

        virtual void LogIn() {
            diagnosticMessageDelegate(
                "MathBot2001",
                1,
                "Logged in."
            );
        }

        virtual void LogOut() {
            if (loggedOut) {
                return;
            }
            diagnosticMessageDelegate(
                "MathBot2001",
                1,
                "Logged out."
            );
            std::lock_guard< decltype(mutex) > lock(mutex);
            loggedOut = true;
            mainThreadEvent.notify_one();
        }

        virtual void Message(
            Twitch::Messaging::MessageInfo&& messageInfo
        ) {
        }

    };

    /**
     * This function is set up to be called when the SIGINT signal is
     * received by the program.  It just sets the "shutDown" flag
     * and relies on the program to be polling the flag to detect
     * when it's been set.
     *
     * @param[in] sig
     *     This is the signal for which this function was called.
     */
    void InterruptHandler(int) {
        shutDown = true;
    }

    /**
     * This function updates the program environment to incorporate
     * any applicable command-line arguments.
     *
     * @param[in] argc
     *     This is the number of command-line arguments given to the program.
     *
     * @param[in] argv
     *     This is the array of command-line arguments given to the program.
     *
     * @param[in,out] environment
     *     This is the environment to update.
     *
     * @param[in] diagnosticMessageDelegate
     *     This is the function to call to publish any diagnostic messages.
     *
     * @return
     *     An indication of whether or not the function succeeded is returned.
     */
    bool ProcessCommandLineArguments(
        int argc,
        char* argv[],
        Environment& environment,
        SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate diagnosticMessageDelegate
    ) {
        std::string tokenFilePath;
        size_t state = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string arg(argv[i]);
            switch (state) {
                case 0: { // next argument
                    if (!tokenFilePath.empty()) {
                        diagnosticMessageDelegate(
                            "MathBot2001",
                            SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                            "multiple token path names given"
                        );
                        return false;
                    }
                    tokenFilePath = arg;
                    state = 0;
                } break;
            }
        }
        if (tokenFilePath.empty()) {
            diagnosticMessageDelegate(
                "MathBot2001",
                SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                "no token path name given"
            );
            return false;
        }
        SystemAbstractions::File tokenFile(tokenFilePath);
        if (!tokenFile.Open()) {
            diagnosticMessageDelegate(
                "MathBot2001",
                SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                SystemAbstractions::sprintf(
                    "unable to open token file '%s'",
                    tokenFile.GetPath().c_str()
                )
            );
            return nullptr;
        }
        std::vector< uint8_t > tokenBuffer(tokenFile.GetSize());
        if (tokenFile.Read(tokenBuffer) != tokenBuffer.size()) {
            diagnosticMessageDelegate(
                "MathBot2001",
                SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                "unable to read token file"
            );
            return nullptr;
        }
        environment.token.assign(
            (const char*)tokenBuffer.data(),
            tokenBuffer.size()
        );
        return true;
    }

    /**
     * This function configures the Twitch messaging object.
     *
     * @param[in,out] tmi
     *     This is the messaging object to configure.
     *
     * @param[in] environment
     *     This contains variables set through the operating system
     *     environment or the command-line arguments.
     *
     * @param[in] user
     *     This is the object which will receive any callbacks from the
     *     Twitch messaging object, for such things as chat messages received.
     *
     * @param[in] diagnosticMessageDelegate
     *     This is the function to call to publish any diagnostic messages.
     */
    void ConfigureMessaging(
        Twitch::Messaging& tmi,
        const Environment& environment,
        std::shared_ptr< Twitch::Messaging::User > user,
        SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate diagnosticMessageDelegate
    ) {
        tmi.SubscribeToDiagnostics(diagnosticMessageDelegate, 0);
        tmi.SetConnectionFactory(
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
        tmi.SetTimeKeeper(std::make_shared< TimeKeeper >());
        tmi.SetUser(user);
    }

}

/**
 * This function is the entrypoint of the program.
 * It just sets up the bot and has it log into Twitch.  At that point, the
 * bot will interact with Twitch using its callbacks.  It registers the
 * SIGINT signal to know when the bot should be shut down.
 *
 * The program is terminated after the SIGINT signal is caught.
 *
 * @param[in] argc
 *     This is the number of command-line arguments given to the program.
 *
 * @param[in] argv
 *     This is the array of command-line arguments given to the program.
 */
int main(int argc, char* argv[]) {
#ifdef _WIN32
    //_crtBreakAlloc = 18;
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif /* _WIN32 */
    const auto previousInterruptHandler = signal(SIGINT, InterruptHandler);
    Environment environment;
    (void)setbuf(stdout, NULL);
    const auto diagnosticsPublisher = SystemAbstractions::DiagnosticsStreamReporter(stderr, stderr);
    if (!ProcessCommandLineArguments(argc, argv, environment, diagnosticsPublisher)) {
        PrintUsageInformation();
        return EXIT_FAILURE;
    }
    Twitch::Messaging tmi;
    const auto bot = std::make_shared< MathBot2001 >();
    bot->diagnosticMessageDelegate = diagnosticsPublisher;
    // TODO: do this when TMI becomes a diagnostics publisher...
    // const auto diagnosticsSubscription = tmi.SubscribeToDiagnostics(diagnosticsPublisher);
    ConfigureMessaging(tmi, environment, bot, diagnosticsPublisher);
    diagnosticsPublisher("MathBot2001", 3, "Configured.");
    tmi.LogIn("MathBot2001", environment.token);
    while (!shutDown) {
        if (bot->AwaitLogOut()) {
            break;
        }
    }
    (void)signal(SIGINT, previousInterruptHandler);
    diagnosticsPublisher("MathBot2001", 3, "Exiting...");
    tmi.LogOut("Bye! BibleThump");
    bot->AwaitLogOut();
    return EXIT_SUCCESS;
}
