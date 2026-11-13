//! @file comms.h
//! @brief Comms: the base-station command layer on top of the Serial module.
//!
//! Validates command lines against the commands the Arduino base-station
//! firmware accepts, sends them, and waits (with a timeout derived from the
//! board's trigger rate and sampling time) for the reply line that carries the
//! command's marker. Used by the main loop only, for TCP clients and the TUI
//! alike; TUI command lines that fail validation are sent unchecked with
//! sendRaw().
//!
//! What each command does and replies is described once, in commandHelp.cpp.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "../SerialModule/serial.h"

//! @brief Sends the base-station's serial commands and waits for their replies.
//!
//! Owns one Serial link (the port and its RX/TX threads) and keeps the host's
//! view of the board: the baud rate in use, whether the continuous `C:` stream
//! is expected, and the trigger rate and sampling time learned from the H/F/M
//! replies, from which the reply timeout is derived (effectiveTimeout()).
//! Destroying it closes the port.
//! @note Not thread-safe: only the main loop calls it. The line taps set with
//!       setSerialTaps() run on the Serial RX/TX threads instead.
class Comms{
public:

    //! @brief Command classification returned by getCommandType().
    //!
    //! - Normal: one-shot commands (P, S, R, E, M, H, F, B) that expect a single
    //!   reply line; sent with sendNormalCommand().
    //! - Continous: the streaming command C, which switches to continuous mode;
    //!   sent with sendContinousCommand(), read with getContinousResponse().
    //! - DelaySearch: the D sweep, per-step lines (51 by default) then one
    //!   result line; run with sendDelaySearch().
    //! - Invalid: unknown command letter, or a missing or malformed argument.
    //!   The main loop drops such TCP commands; TUI lines go out through
    //!   sendRaw() instead.
    enum class CommandType { Normal, Continous, DelaySearch, Invalid };

    //! @brief Open the serial port and start its RX/TX threads.
    //!
    //! The port, rate and timeout are recorded first, even when the call fails,
    //! so getSerialPort()/getBaudRate() report the link last asked for. A rate
    //! the host does not support is refused before anything is closed;
    //! otherwise a session still running is ended first. Bytes pending at open
    //! are dropped.
    //! @param portName Serial device, e.g. `/dev/ttyACM0`.
    //! @param baudRate Host baud rate; one that hostSupportsBaud() accepts.
    //! @param timeout  Reply timeout in ms (see setResponseTimeout()).
    //! @return 0 on success, -1 if the host does not support the rate or the
    //!         port cannot be opened.
    //! @note Does not wait for the board: opening a USB CDC port resets the Uno
    //!       through DTR, so wait for it to boot (~7 s) before sending
    //!       anything, then call querySettings().
    int begin(std::string portName, int baudRate, int timeout = 500);

    //! @brief Stop the serial RX/TX threads and close the port (Serial::end()).
    //!
    //! Safe to call when the port is already closed.
    void end();

    //! @brief Ask the board for its stored trigger rate and sampling time.
    //!
    //! Sends `F 0`, falling back to `H 0` for firmware without 'F'. Both values
    //! are out of range, so nothing changes and the reply reports the values in
    //! force; sendNormalCommand() records them (noteReply()), and the reply
    //! timeout and the 'D' deadline follow them. Like any normal command it
    //! ends continuous mode. Call only once the board has booted, i.e. after
    //! the settle time following begin()/reopen().
    //! @return true if either query was answered, false if neither was (a
    //!         warning is logged, only with VERBOSE_LOGGING=ON, and the previous values are kept).
    //! @note Blocks for up to two reply timeouts (effectiveTimeout()).
    bool querySettings();

    //! @brief Close and re-open the serial port, possibly on a different device
    //!        or at another rate.
    //!
    //! Leaves continuous mode and keeps the current reply timeout. If the new
    //! port/rate fails to open, the previous one is opened again (if that fails
    //! too, the link stays closed).
    //! @param portName Serial device to open.
    //! @param baudRate Host baud rate. An unsupported rate is refused before
    //!                 the working link is closed.
    //! @return 0 on success, -1 if the baud rate is not supported (the link is
    //!         left as it was) or the port fails to open.
    //! @note Opening a USB CDC port resets the Uno via DTR, so the caller must
    //!       wait for the board to boot (~7 s) before sending; this also applies
    //!       after a failed reopen whose rollback re-opened the old port.
    int reopen(const std::string& portName, int baudRate);

    //! @brief Switch the open port to another baud rate in place.
    //!
    //! No close/reopen, so no DTR reset of the Uno: the main loop uses it to
    //! follow a base-station that rebooted at another rate after 'B' or 'M'.
    //! Drops anything received before the switch (Serial::setBaudRate()). On
    //! success the host also leaves continuous mode (nothing is sent to the
    //! board) and getBaudRate() reports the new rate.
    //! @param baudRate New host baud rate; one that hostSupportsBaud() accepts.
    //! @return 0 on success, -1 if the host does not support the rate or the
    //!         port is not open (or refuses the speed).
    int setBaudRate(int baudRate);

    //! @brief Whether the host side can open the serial port at a rate.
    //! @param baudRate Baud rate to check.
    //! @return true for 9600, 19200, 38400, 57600 and 115200 (keep in sync with
    //!         the --baud-rate choices), false for any other rate.
    static bool hostSupportsBaud(long baudRate);

    //! @brief Whether a serial session is running (port open, RX/TX threads
    //!        alive).
    //! @return true from a successful begin()/reopen() until end(), or until
    //!         the serial RX thread ends the session after a read or poll error
    //!         or a hang-up (see Serial::isOpen()).
    bool isOpen() const { return serial.isOpen(); }

    //! @brief Host baud rate last asked for.
    //! @return The rate recorded by the last begin() (even if that open failed;
    //!         reopen() goes through begin()), or the one set by a successful
    //!         setBaudRate().
    int getBaudRate() const { return baudRateInt; }

    //! @brief Serial device last asked for.
    //! @return The device recorded by the last begin(), even if that open
    //!         failed.
    const std::string& getSerialPort() const { return serialPort; }

    //! @brief Set the minimum time to wait for a reply.
    //! @param ms Reply timeout in ms; effectiveTimeout() may use a longer one.
    void setResponseTimeout(unsigned ms) { responseTimeout = ms; }

    //! @brief Reply timeout as set by begin() or setResponseTimeout().
    //! @return The timeout in ms, before effectiveTimeout() raises it.
    unsigned getResponseTimeout() const { return responseTimeout; }

    //! @brief Forward line taps to the serial layer (see Serial::LineTap).
    //!
    //! May be called before begin(); taps survive end(), begin() and reopen().
    //! @param rx Called with each line received from the board (see
    //!           Serial::setRxTap()); empty for none.
    //! @param tx Called with each line written to the board (see
    //!           Serial::setTxTap()); empty for none.
    //! @note The taps run on the Serial RX/TX threads.
    void setSerialTaps(Serial::LineTap rx, Serial::LineTap tx);

    //! @brief Run the 'D' burst-delay search and wait for its result line.
    //!
    //! Queries the trigger rate and sampling time first (querySettings()) and
    //! derives the deadline and the quiet window from them, then sends `D` and
    //! collects the `D:` lines. Blocks until the final `D:` line arrives (the
    //! 52nd with the firmware's default search settings, otherwise the last one
    //! before the stream goes quiet), the deadline passes, or `*abort` becomes
    //! true.
    //! @param finalLine Receives each `D:` line as it arrives; when the call
    //!                  returns true, the last `D:` line received: the result
    //!                  line `D: <best_us>, <mean>, <valid>/<total>` when
    //!                  `*confirmed` is true; after an unconfirmed finish it may
    //!                  be a step line.
    //! @param timeoutMs Deadline in ms. A floor: the real deadline grows with the
    //!                  trigger period and the sampling time (51 steps + 5 s).
    //! @param abort     Optional flag polled while waiting for the `D:` lines;
    //!                  once true, the call returns false (nothing is sent to
    //!                  the board, which finishes the sweep anyway).
    //! @param confirmed Optional; set true only when the end of the sweep was
    //!                  recognised (52nd line, or the line after the last step),
    //!                  not just a quiet stream; false otherwise.
    //! @return true when the sweep ended (recognised, or the stream went quiet
    //!         after at least one `D:` line), false on timeout or abort.
    //! @note Leaves continuous mode. Blocks the caller for the whole sweep
    //!       (about 16 s at 10 Hz).
    bool sendDelaySearch(std::string& finalLine, unsigned timeoutMs = 25000,
                         const std::atomic<bool>* abort = nullptr,
                         bool* confirmed = nullptr);

    //! @brief Trigger period derived from the trigger rate.
    //! @return 1000 / trigger rate, in ms, rounded up (100 ms at 10 Hz).
    unsigned triggerPeriodMs() const { return (1000u + triggerRateHz - 1u) / triggerRateHz; }

    //! @brief Reply timeout actually used by sendNormalCommand(),
    //!        sendContinousCommand() and getContinousResponse().
    //! @return max(set timeout, trigger period + sampling time + 200 ms), in ms.
    unsigned effectiveTimeout() const;

    //! @brief Send an arbitrary line without validating it or waiting for a
    //!        reply.
    //!
    //! Used for TUI command lines that fail validation; a newline is appended.
    //! Neither the stored line nor the continuous-mode flag is touched; any
    //! reply only shows up through the RX tap.
    //! @param line Line to send, without its newline.
    void sendRaw(const std::string& line);

    //! @brief Send a one-shot command and wait for its reply line.
    //!
    //! Leaves continuous mode and drops stale lines (the continuous stream, or a
    //! reply that arrived after an earlier timeout) before sending, so that they
    //! are not taken for this command's reply. The reply is the first line that
    //! starts with the command's marker and a colon (see responseMarker); other
    //! lines are skipped. H/F/M replies update the trigger rate and sampling
    //! time behind effectiveTimeout() (noteReply()).
    //! @param command  Command line without its newline, e.g. `P` or `H 80`;
    //!                 not validated here (see getCommandType()).
    //! @param response Receives the reply line (any trailing CR kept);
    //!                 unchanged on timeout.
    //! @return true if the reply arrived within effectiveTimeout(), false on
    //!         timeout.
    bool sendNormalCommand(std::string command, std::string &response);

    //! @brief Send the continuous-mode command (C) and wait for the stream to
    //!        start.
    //!
    //! Sets the continuous-mode flag before sending (it stays set even if no
    //! line arrives) and drops stale lines, then waits for the first `C:` line,
    //! which is only logged (only with VERBOSE_LOGGING=ON): read the stream with
    //! getContinousResponse().
    //! @param command Command line without its newline (`C`).
    //! @return true if a `C:` line arrived within effectiveTimeout(), false on
    //!         timeout.
    bool sendContinousCommand(std::string command);

    //! @brief Wait for the next line of the continuous stream.
    //!
    //! Serial keeps only the latest line, so a line that is not taken before
    //! the next one arrives is skipped here (the RX tap still sees it).
    //! @param response Receives the `C:` line (any trailing CR kept);
    //!                 unchanged on timeout.
    //! @return true if a `C:` line arrived within effectiveTimeout(), false on
    //!         timeout.
    bool getContinousResponse(std::string &response);

    //! @brief Whether the host expects the continuous `C:` stream.
    //! @return true after sendContinousCommand(), until sendNormalCommand(),
    //!         sendDelaySearch(), reopen() or a successful setBaudRate().
    bool isContinousMode() const { return continousMode; }

    //! @name Command validation
    //! All three helpers accept the raw line as received (e.g. `P` with its
    //! newline, from TCP) and work on it directly, so the caller does not need
    //! to strip trailing whitespace first. Leading whitespace is not stripped.
    //! @{

    //! @brief Strip trailing LF/CR/space/tab characters.
    //! @param rawCmd Command line as received.
    //! @return The clean command string.
    std::string extractCommand(const std::string& rawCmd) const;

    //! @brief Whether the raw command maps to a known Arduino command.
    //! @param rawCmd Command line as received.
    //! @return true unless getCommandType() returns CommandType::Invalid.
    bool isValidCommand(const std::string& rawCmd) const;

    //! @brief Classify a raw command line.
    //!
    //! Case-sensitive. P, S, C, R, D, E and M must be the letter alone; H, F and
    //! B need exactly one integer argument (`H 80` or `H80`: optional blanks,
    //! an optional sign, 1 to 9 digits, then only blanks). Only the syntax is
    //! checked: an out-of-range value is left to the firmware, where it changes
    //! nothing and acts as a query.
    //! @param rawCmd Command line as received.
    //! @return The command's CommandType, or CommandType::Invalid if the letter
    //!         is unknown or the argument is missing or malformed.
    CommandType getCommandType(const std::string& rawCmd) const;

    //! @}

private:
    //! Reply timeout in ms (begin()/setResponseTimeout(); see effectiveTimeout())
    unsigned int responseTimeout = 500;
    //! Firmware default sampling time in ms (SAMPLING_TIME_MS)
    static constexpr unsigned defaultSamplingTimeMs = 80;
    //! Sampling time in force on the base station, in ms, as tracked by
    //! noteReply() (starts at the firmware default)
    unsigned samplingTimeMs = defaultSamplingTimeMs;
    //! Firmware default trigger rate in Hz (TRIGGER_RATE_HZ)
    static constexpr unsigned defaultTriggerRateHz = 10;
    //! Trigger rate in force on the base station, in Hz, as tracked by
    //! noteReply() (starts at the firmware default)
    unsigned triggerRateHz = defaultTriggerRateHz;
    //! @brief Track H/F/M replies that change the sampling time / trigger rate.
    //!
    //! `H: <ms>, ...` sets the sampling time, `F: <Hz>, <TOP>, <ms>` the
    //! trigger rate and the sampling time, `M: OK` restores both defaults.
    //! Values outside 1-1000 ms or 1-50 Hz are ignored, and so are other lines.
    //! @param line Reply line taken by sendNormalCommand().
    void noteReply(const std::string& line);
    //! True while the host expects the continuous `C:` stream (see
    //! isContinousMode())
    bool continousMode = false;

    //! Serial port and its RX/TX threads
    Serial serial;
    //! Host baud rate (see getBaudRate())
    uint32_t baudRateInt = 38400;
    //! Serial device path (see getSerialPort())
    std::string serialPort;

    //! @name Reply markers
    //! A reply line starts with `<marker>:`. Normal commands use the marker in
    //! responseMarker (normalCommandMarker, `N`, for a letter not listed
    //! there), the continuous stream `C`, the delay search `D`.
    //! @{
    //! Fallback marker for a normal command (`N:`)
    const std::string normalCommandMarker = "N";
    //! Marker of the continuous stream lines (`C:`)
    const std::string continousCommandMarker = "C";
    //! Marker of the delay search lines (`D:`)
    const std::string delaySearchMarker = "D";
    //! @}

    //! Reply marker per normal command, keyed by command letter
    //! (P/S/R answer `N: ...`, E `E: <us>`, M `M: OK`, H `H: <ms>, <Timer1 counts>`,
    //!  F `F: <Hz>, <Timer1 TOP>, <sampling ms>`, B `B: <stored baud>`: the new rate after a
    //!  change, the current one for a query)
    const std::unordered_map<std::string, std::string> responseMarker = {
        { "P", "N" }, { "S", "N" }, { "R", "N" }, { "E", "E" },
        { "M", "M" }, { "H", "H" }, { "F", "F" }, { "B", "B" },
    };

    //! @brief Wait for a received line that starts with marker.
    //!
    //! Polls Serial::getReceived() every 10 ms until effectiveTimeout() has
    //! passed; lines that do not start with the marker are skipped.
    //! @param response Receives the matching line; unchanged on timeout.
    //! @param marker   Line prefix to wait for, colon included (e.g. `N:`).
    //! @return true if a matching line arrived in time, false on timeout (a
    //!         warning with the last line seen is logged, only with VERBOSE_LOGGING=ON).
    bool checkResponse(std::string &response, const std::string marker);

    //! Lookup table: command letter → CommandType. Matches the Arduino
    //! base-station command table (command_control.cpp). H, F and B need one
    //! integer argument (see intArgCommands); an out-of-range value is a query
    //! on the firmware side. 'B' and 'M' can reboot the base-station at another
    //! baud rate: the main loop follows it (main.cpp followBaudChange();
    //! described in commandHelp.cpp).
    const std::unordered_map<std::string, CommandType> validCommands = {
        { "P", CommandType::Normal      },
        { "S", CommandType::Normal      },
        { "C", CommandType::Continous   },
        { "R", CommandType::Normal      },
        { "D", CommandType::DelaySearch },
        { "E", CommandType::Normal      },
        { "M", CommandType::Normal      },  // settings reset, "M: OK"
        { "H", CommandType::Normal      },  // "H <ms>", needs one integer argument
        { "F", CommandType::Normal      },  // "F <Hz>", needs one integer argument
        { "B", CommandType::Normal      },  // "B <baud>", needs one integer argument
    };

    //! Commands that take exactly one integer argument (`H 100` or `H100`)
    const std::unordered_set<std::string> intArgCommands = { "H", "F", "B" };
};
