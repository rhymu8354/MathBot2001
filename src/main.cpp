/**
 * @file main.cpp
 *
 * This module holds the main() function, which is the entrypoint
 * to the program.
 *
 * © 2018 by Richard Walters
 */

#include "MathBot2001.hpp"

#include <condition_variable>
#include <mutex>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <StringExtensions/StringExtensions.hpp>
#include <SystemAbstractions/DiagnosticsStreamReporter.hpp>
#include <SystemAbstractions/File.hpp>
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
                "Usage: MathBot2001 TOKEN CHANNEL [NICK]\n"
                "\n"
                "Connect to Twitch chat and listen for messages.\n"
                "\n"
                "  TOKEN   Path/name of file containing the OAuth token to use\n"
                "  CHANNEL Name of the Twitch channel to join\n"
                "  NICK    Nickname (username) to use (default: MathBot2001)\n"
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

        /**
         * This is the name of the channel to join in Twitch.
         */
        std::string channel;

        /**
         * This is the nickname to use on Twitch.
         */
        std::string nickname = "MathBot2001";
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
        enum class State {
            Token,
            Channel,
            Nickname,
            Done,
        } state = State::Token;
        std::string tokenFilePath;
        for (int i = 1; i < argc; ++i) {
            const std::string arg(argv[i]);
            switch (state) {
                case State::Token: {
                    tokenFilePath = arg;
                    state = State::Channel;
                } break;

                case State::Channel: {
                    environment.channel = arg;
                    state = State::Nickname;
                } break;

                case State::Nickname: {
                    environment.nickname = arg;
                    state = State::Done;
                } break;

                default: {
                } break;
            }
        }
        if (state == State::Token) {
            diagnosticMessageDelegate(
                "MathBot2001",
                SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                "no token path name given"
            );
            return false;
        } else if (state == State::Channel) {
            diagnosticMessageDelegate(
                "MathBot2001",
                SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                "no channel name given"
            );
            return false;
        }
        SystemAbstractions::File tokenFile(tokenFilePath);
        if (!tokenFile.OpenReadOnly()) {
            diagnosticMessageDelegate(
                "MathBot2001",
                SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                StringExtensions::sprintf(
                    "unable to open token file '%s'",
                    tokenFile.GetPath().c_str()
                )
            );
            return false;
        }
        std::vector< uint8_t > tokenBuffer(tokenFile.GetSize());
        if (tokenFile.Read(tokenBuffer) != tokenBuffer.size()) {
            diagnosticMessageDelegate(
                "MathBot2001",
                SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                "unable to read token file"
            );
            return false;
        }
        environment.token.assign(
            (const char*)tokenBuffer.data(),
            tokenBuffer.size()
        );
        return true;
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
    const auto bot = std::make_shared< MathBot2001 >();
    bot->Configure(diagnosticsPublisher);
    bot->InitiateLogIn(
        environment.token,
        environment.channel,
        environment.nickname
    );
    while (!shutDown) {
        if (bot->AwaitLogOut()) {
            break;
        }
    }
    (void)signal(SIGINT, previousInterruptHandler);
    bot->InitiateLogOut();
    bot->AwaitLogOut();
    return EXIT_SUCCESS;
}
