#ifndef MATH_BOT_2001_HPP
#define MATH_BOT_2001_HPP

/**
 * @file MathBot2001.hpp
 *
 * This module declares the MathBot2001 implementation.
 *
 * © 2018 by Richard Walters
 */

#include <memory>
#include <string>
#include <SystemAbstractions/DiagnosticsSender.hpp>

/**
 * This represents the chat bot itself.  It handles any callbacks
 * received from the Twitch messaging interface.
 */
class MathBot2001 {
    // Lifecycle Methods
public:
    ~MathBot2001() noexcept;
    MathBot2001(const MathBot2001&) = delete;
    MathBot2001(MathBot2001&&) noexcept = delete;
    MathBot2001& operator=(const MathBot2001&) = delete;
    MathBot2001& operator=(MathBot2001&&) noexcept = delete;

    // Public Methods
public:
    /**
     * This is the constructor of the class.
     */
    MathBot2001();

    /**
     * This method sets up the bot to interact with the app and with
     * Twitch chat.
     *
     * @param[in] diagnosticMessageDelegate
     *     This is the function to call to publish any diagnostic messages.
     */
    void Configure(
        SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate diagnosticMessageDelegate
    );

    /**
     * This method is called to initiate logging into Twitch chat.
     *
     * @param[in] token
     *     This is the OAuth token to use in authenticating with Twitch.
     *
     * @param[in] channel
     *     This is the channel in which to participate in chat.
     */
    void InitiateLogIn(
        const std::string& token,
        const std::string& channel
    );

    /**
     * This method is called to initiate logging out of Twitch chat.
     */
    void InitiateLogOut();

    /**
     * This method waits up to a quarter second for the bot to be
     * logged out of Twitch.
     *
     * @return
     *     An indication of whether or not the bot has been logged
     *     out of Twitch is returned.
     */
    bool AwaitLogOut();

    // Private properties
private:
    /**
     * This is the type of structure that contains the private
     * properties of the instance.  It is defined in the implementation
     * and declared here to ensure that it is scoped inside the class.
     */
    struct Impl;

    /**
     * This contains the private properties of the instance.
     */
    std::unique_ptr< Impl > impl_;
};

#endif /* MATH_BOT_2001_HPP */
