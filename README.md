# MathBot2001

This is a stand-alone program which operates as a bot in Twitch chat.  It creates an instance of the `Twitch::Messaging` class and provides it with a factory for creating instances of the `TwitchNetworkTransport::Connection` class in order to connect to the Twitch chat server, join a channel, and participate in chat.

## Usage

    Usage: MathBot2001 TOKEN CHANNEL

    Connect to Twitch chat and listen for messages.

      TOKEN   Path/name of file containing the OAuth token to use
      CHANNEL Name of the Twitch channel to join

MathBot2001 connects to Twitch chat, joins a specific channel, and reports any chat messages posted to the channel.

## Supported platforms / recommended toolchains

This is a portable C++11 library which depends only on the C++11 compiler and standard library, so it should be supported on almost any platform.  The following are recommended toolchains for popular platforms.

* Windows -- [Visual Studio](https://www.visualstudio.com/) (Microsoft Visual C++)
* Linux -- clang or gcc
* MacOS -- Xcode (clang)

## Building

This library is not intended to stand alone.  It is intended to be included in a larger solution which uses [CMake](https://cmake.org/) to generate the build system and build applications which will link with the library.

There are two distinct steps in the build process:

1. Generation of the build system, using CMake
2. Compiling, linking, etc., using CMake-compatible toolchain

### Prerequisites

* [CMake](https://cmake.org/) version 3.8 or newer
* C++11 toolchain compatible with CMake for your development platform (e.g. [Visual Studio](https://www.visualstudio.com/) on Windows)
* [StringExtensions](https://github.com/rhymu8354/StringExtensions.git) - a
  library containing C++ string-oriented libraries, many of which ought to be
  in the standard library, but aren't.
* [SystemAbstractions](https://github.com/rhymu8354/SystemAbstractions.git) - a cross-platform adapter library for system services whose APIs vary from one operating system to another

### Build system generation

Generate the build system using [CMake](https://cmake.org/) from the solution root.  For example:

```bash
mkdir build
cd build
cmake -G "Visual Studio 15 2017" -A "x64" ..
```

### Compiling, linking, et cetera

Either use [CMake](https://cmake.org/) or your toolchain's IDE to build.
For [CMake](https://cmake.org/):

```bash
cd build
cmake --build . --config Release
```
