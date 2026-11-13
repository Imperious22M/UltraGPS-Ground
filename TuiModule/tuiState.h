//! @file tuiState.h
//! @brief Shared state between the ground-station main loop, the serial RX/TX
//!        threads and the terminal UI thread: TuiState, the types it hands
//!        out, and the ControlQueue of TUI requests.
//!
//! Nothing in here depends on notcurses, so it is compiled into every build;
//! the main loop always drains the ControlQueue (it is simply empty when the
//! TUI is not running), and headless runs use TuiState for the base-station
//! boot wait (setSettleUntil() / settling()).
//!
//! Threading rules:
//! - TuiState is written by the serial taps (RX/TX threads), the spdlog sink
//!   (any thread), the main loop and the TUI thread (help text); it is read by
//!   the TUI thread and, via settling(), by the main loop. Every member is
//!   guarded by one mutex; the TUI draws from a TuiSnapshot copy. The taps
//!   and the sink are only attached when the TUI runs.
//! - ControlQueue is the only way the TUI asks for something to happen. The
//!   main loop owns Comms/ServerModule and executes requests on its thread,
//!   so the TUI never races checkResponse()/clearReadBuffer()/reopen().

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

//! Number of receivers reported by the base-station (RECV_CNT in the firmware)
constexpr size_t TUI_RECV_CNT = 6;

//! Tick counts of one measurement line, one per receiver (0 = no echo)
using TickArray = std::array<uint32_t, TUI_RECV_CNT>;

//! @brief Parse "N: t0, t1, t2, t3, t4, t5" or "C: ..." into six tick counts.
//!
//! Spaces and tabs may surround the numbers; only blanks and a CR may follow
//! the last one.
//! @param line Line received from the base-station.
//! @param out  Receives the six counts; left unchanged on failure.
//! @return false for anything else (e.g. "N: R", "E: 2000", "D: ..."), and for
//!         a count that does not fit in 32 bits.
bool parseTicks(const std::string& line, TickArray& out);

//! @name Base-station firmware defaults
//! In force after 'M' (settings-eeprom.h).
//! @{

constexpr long BASE_DEFAULT_DELAY_US = 2000;   //!< SONIC_BURST_DELAY: burst delay in us
constexpr long BASE_DEFAULT_SAMPLING_MS = 80;   //!< SAMPLING_TIME_MS: sampling time in ms
constexpr long BASE_DEFAULT_SAMPLING_COUNTS = 5000;  //!< 80 ms in 16 us Timer1 counts
constexpr long BASE_DEFAULT_RATE_HZ = 10;            //!< TRIGGER_RATE_HZ (radio.h)
constexpr long BASE_DEFAULT_TIMER1_TOP = 6249;       //!< triggerRateToTop(10)
constexpr long BASE_DEFAULT_BAUD = 38400;            //!< BAUD_RATE (settings-eeprom.h)

//! @}

//! Trigger rates 'F' accepts, in Hz: TRIGGER_RATE_MIN_HZ / TRIGGER_RATE_MAX_HZ (radio.h)
constexpr long BASE_RATE_MIN_HZ = 1, BASE_RATE_MAX_HZ = 50;
//! Sampling times 'H' accepts, in ms: SAMPLING_TIME_MIN_MS / SAMPLING_TIME_MAX_MS
//! (settings-eeprom.h); the trigger rate can lower the maximum, see baseMaxSamplingMs()
constexpr long BASE_SAMPLING_MIN_MS = 1, BASE_SAMPLING_MAX_MS = 1000;

//! @name Tick jitter
//! Tick jitter of a receiver: |mean of its non-zero ticks received in the last
//! JITTER_SHORT_WINDOW - mean of those received in the last JITTER_LONG_WINDOW|,
//! in ticks (exact rolling windows, computed by TuiState::snapshot()). Shown
//! under each receiver on the MAIN screen; a high value means its readings
//! are changing a lot.
//! @{

constexpr auto JITTER_SHORT_WINDOW = std::chrono::seconds(1);   //!< window of the current mean
constexpr auto JITTER_LONG_WINDOW = std::chrono::seconds(10);   //!< window of the reference mean

//! TUI colour steps for the jitter, in ticks: green below JITTER_AMBER_TICKS,
//! amber from it, red from JITTER_RED_TICKS
constexpr long JITTER_AMBER_TICKS = 5, JITTER_RED_TICKS = 20;

//! @}

//! @brief Largest sampling time 'H' accepts for a Timer1 TOP: 80 % of the
//!        trigger period, at most BASE_SAMPLING_MAX_MS
//!        (UltrasonicSampling::maxSamplingTimeMs() in the firmware).
//! @param timer1Top Timer1 TOP in force (BaseInfo::timer1Top); 0 or less when
//!                  unknown.
//! @return The limit in ms, rounded down; BASE_SAMPLING_MAX_MS when the TOP is
//!         unknown.
inline long baseMaxSamplingMs(long timer1Top){
    if(timer1Top <= 0) return BASE_SAMPLING_MAX_MS;
    //! a trigger period is TOP + 1 Timer1 counts of 16 us
    long ms = (timer1Top + 1) * 80 / 100 * 16 / 1000;
    return ms > BASE_SAMPLING_MAX_MS ? BASE_SAMPLING_MAX_MS : ms;
}

//! @brief Base-station state learned from its replies (nothing is known at
//!        first: settings are -1, texts empty, times never).
//!
//! Filled by TuiState::onRx() from the reply lines of the firmware commands:
//! - "E: <us>"                           burst delay in force ('E')
//! - "H: <ms>, <Timer1 counts>"          sampling time in force ('H')
//! - "F: <Hz>, <Timer1 TOP>, <ms>"       trigger rate + sampling time in force ('F')
//! - "D: <us>, <mean>, <valid>/<total>"  delay-search step or result ('D')
//! - "B: <baud>"                         serial baud rate stored in EEPROM ('B';
//!                                       the board reboots into a new one)
//! - "M: OK"                             settings reset to defaults ('M')
//! - "N: R"                              transmitter reset sent ('R')
//!
//! TuiState::onSearchResult() adds the result of a 'D' sweep, and
//! TuiState::onTx() restarts the step count when 'D' is sent.
//!
//! The transmitter station has no link to the ground station: it only learns
//! the burst delay (in every trigger packet), the trigger rate (Timer1 TOP,
//! sent on 'F', 'M' and every base-station boot; it stores a TOP that differs
//! from its own and reboots) and resets ('R') over the radio, so its state is
//! tracked from the base-station side.
struct BaseInfo {
    long burstDelayUs = -1;     //!< from 'E' or a 'D' result with valid readings
    long samplingMs = -1;       //!< from 'H' or 'F'
    long samplingCounts = -1;   //!< from 'H' (16 us Timer1 counts); recomputed when
                                //!< an 'F' reply changes the sampling time
    long triggerHz = -1;        //!< from 'F'
    long timer1Top = -1;        //!< from 'F'
    long baud = -1;             //!< from 'B' (stored in EEPROM, 38400 after 'M')
    std::string searchResult;   //!< last 'D' result "<us> US <v>/<t> MEAN <m>", followed
                                //!< by "  UNCONFIRMED" or "  NOT STORED" when so
    int searchSteps = 0;        //!< 'D' lines received since the last 'D' was sent (a
                                //!< confirmed result line is taken off again)
    std::string lastReply;      //!< last recognised reply (E/H/F/B/D, M: OK, N: R)

    //! @name Transmitter station (radio side)
    //! Wall-clock times of the last packets sent to it; the default (epoch)
    //! means never.
    //! @{

    std::chrono::system_clock::time_point txResetTime{};   //!< last "N: R" (reset packet sent)
    std::chrono::system_clock::time_point txRateTime{};    //!< last 'F <1..50>' / 'M' reply (rate packet sent)

    //! @}
};

//! @brief Parse "D: <us>, <mean>, <valid>/<total>" (a 'D' step or result line).
//! @param line  Line received from the base-station.
//! @param us    Receives the burst delay, in us.
//! @param mean  Receives the mean tick count of the valid readings.
//! @param valid Receives the number of valid (non-zero) readings.
//! @param total Receives the number of readings taken.
//! @return false for anything else, trailing text included; the outputs are
//!         then left unchanged.
bool parseDelayLine(const std::string& line, long& us, long& mean, long& valid, long& total);

//! @brief One line of the serial "chat log"
struct ChatLine {
    //! @brief Origin: Tx sent to the base-station ('>' in the log pane), Rx
    //!        received from it ('<'), Log a spdlog message, Sys a TUI /
    //!        main-loop message ('*')
    enum class Kind { Tx, Rx, Log, Sys };
    std::chrono::system_clock::time_point t;   //!< wall-clock time it was added
    Kind kind;          //!< origin of the line
    int level;          //!< spdlog::level::level_enum for Log lines, 0 otherwise
    std::string text;   //!< the line, without CR/LF
};

//! @brief One second of tick history (per-receiver max and non-zero min in that second)
struct TickBucket {
    int64_t sec;        //!< steady_clock seconds (immune to wall-clock jumps)
    TickArray max;      //!< largest reading of each receiver; 0 = no echo all second
    TickArray min;      //!< smallest non-zero reading, 0 = none
};

//! @brief Copy of everything the TUI draws, taken under the lock in one go
//!        (TuiState::snapshot()).
struct TuiSnapshot {
    TickArray lastTicks{};      //!< counts of the last tick line ("N:" / "C:")
    bool haveTicks = false;     //!< a tick line has been received
    std::chrono::steady_clock::time_point lastTickTime{};   //!< when the last tick line arrived
    std::deque<TickBucket> history;     //!< per-second history, oldest first; see
                                        //!< TuiState::historySeconds

    //! per receiver: tick jitter (see JITTER_SHORT_WINDOW), -1 = no reading in a window
    std::array<long, TUI_RECV_CNT> jitter{};
    uint64_t rxCount = 0, txCount = 0;  //!< lines received from / sent to the base-station

    //! @name Link state
    //! Published by the main loop (TuiState::setLink(), setSettleUntil(),
    //! setBusy()).
    //! @{

    std::string serialPort;     //!< serial device path
    int baud = 0;               //!< host baud rate (the base-station's is BaseInfo::baud)
    bool linkOpen = false;      //!< port open and its RX/TX threads running
    bool continuous = false;    //!< Comms is in continuous ('C') mode
    unsigned responseTimeout = 0;   //!< reply timeout set on Comms, in ms
    int settleSecondsLeft = 0;  //!< seconds left of the base-station boot wait,
                                //!< rounded up; 0 when not booting
    std::string busy;   //!< non-empty while a long operation (delay search) runs
    std::string logLevel;       //!< spdlog level name

    //! @}

    BaseInfo base;              //!< what the base-station last reported
};

//! @brief State shared by the serial taps, the spdlog sink, the main loop and
//!        the TUI.
//!
//! Holds the chat log, the tick readout with its per-second history and
//! jitter windows, the link state and what the base-station last reported.
//! @note Every method is thread-safe: each one holds the single mutex m_ for
//!       its whole run. The TUI draws from snapshot() copies.
class TuiState {
public:
    static constexpr size_t maxChatLines = 1000;    //!< chat log length; older lines are dropped
    static constexpr int historySeconds = 60;       //!< seconds of tick history kept (graph span)

    //! @brief Serial RX tap: one line received from the base-station.
    //!
    //! Counts it and adds it to the chat log. A tick line (parseTicks())
    //! becomes the last reading and goes into the per-second history and the
    //! jitter windows; a recognised reply updates BaseInfo.
    //! @param line The line, CR stripped.
    //! @note Called on the serial RX thread (Serial::LineTap).
    void onRx(const std::string& line);
    //! @brief Serial TX tap: one line written to the base-station.
    //!
    //! Counts it and adds it to the chat log. "D" restarts the delay-search
    //! step count; an 'F' line notes whether it sets a rate (1..50 Hz) or only
    //! queries it, which the reply's handling in onRx() needs.
    //! @param line The line, CR/LF stripped.
    //! @note Called on the serial TX thread (Serial::LineTap).
    void onTx(const std::string& line);
    //! @brief spdlog callback sink (level is spdlog::level::level_enum).
    //! @param level spdlog level of the message.
    //! @param text  The message, without time or level prefix.
    //! @note Called on whichever thread logs.
    void onLog(int level, const std::string& text);
    //! @brief Messages from the TUI / main loop itself ('*' lines in the log).
    //! @param text The message.
    void sys(const std::string& text);
    //! @brief Several messages as one uninterrupted block of the chat log.
    //!
    //! They are added under one lock with one timestamp, so no other line
    //! lands between them (used for the command reference).
    //! @param lines The messages, in order.
    void sysLines(const std::vector<std::string>& lines);

    //! @brief Link state published by the main loop.
    //! @param port       Serial device path.
    //! @param baud       Host baud rate.
    //! @param open       Port open and its RX/TX threads running.
    //! @param continuous Comms is in continuous ('C') mode.
    //! @param timeout    Reply timeout set on Comms, in ms.
    //! @param logLevel   spdlog level name.
    void setLink(const std::string& port, int baud, bool open, bool continuous,
                 unsigned timeout, const std::string& logLevel);
    //! @brief Set the end of the base-station boot wait.
    //!
    //! Until then settling() is true: TUI and TCP commands are not sent and
    //! the header shows BOOTING. Replaces any earlier end time.
    //! @param t End of the wait.
    void setSettleUntil(std::chrono::steady_clock::time_point t);
    //! @brief Whether the base-station boot wait is still running.
    //! @return true until the time set by setSettleUntil().
    bool settling() const;
    //! @brief Set the banner shown while a long operation runs.
    //! @param what Banner text (e.g. "DELAY SEARCH IN PROGRESS"); empty clears it.
    void setBusy(const std::string& what);
    //! @brief Final line of a completed 'D' sweep (Comms::sendDelaySearch()).
    //!
    //! Records the result text. When confirmed, the result line is taken off
    //! the step count and, if it had valid readings, its delay becomes the
    //! burst delay in force (as the firmware stores it); without valid
    //! readings the text is marked NOT STORED.
    //! @param line      The "D: <us>, <mean>, <valid>/<total>" line; anything
    //!                  else is ignored.
    //! @param confirmed true if Comms recognised the end of the sweep. false:
    //!                  the sweep end was not recognised, nothing is assumed
    //!                  stored (the text is marked UNCONFIRMED).
    void onSearchResult(const std::string& line, bool confirmed);

    //! @brief Copy of the display state, taken under the lock in one go.
    //!
    //! Also fills in TuiSnapshot::settleSecondsLeft and each receiver's
    //! jitter, after dropping the samples that have left the jitter windows.
    //! @return The copy.
    TuiSnapshot snapshot() const;

    //! @brief Copy chat lines. offsetFromEnd lines are skipped at the end
    //!        (scrollback); at most count lines are returned, oldest first.
    //! @param count         Most lines to return.
    //! @param offsetFromEnd Number of newest lines to skip.
    //! @return The lines; empty when offsetFromEnd is not below chatSize().
    std::deque<ChatLine> chat(size_t count, size_t offsetFromEnd) const;
    //! @brief Number of lines in the chat log.
    //! @return At most maxChatLines.
    size_t chatSize() const;

private:
    //! @brief Append a line to the chat log, dropping the oldest beyond
    //!        maxChatLines.
    //! @param line The line.
    //! @note The caller holds m_.
    void pushChat(ChatLine line);

    //! @brief Running sum and count of the non-zero ticks of each receiver in a window
    struct WindowSums {
        std::array<uint64_t, TUI_RECV_CNT> sum{};       //!< sum of the non-zero ticks
        std::array<uint32_t, TUI_RECV_CNT> count{};     //!< number of non-zero ticks

        //! @brief Add the non-zero ticks of a line entering the window.
        //! @param t The line's ticks.
        void add(const TickArray& t);
        //! @brief Take out a line added earlier that left the window.
        //! @param t The line's ticks.
        void remove(const TickArray& t);
    };
    //! @brief One tick line with the steady_clock time it arrived
    struct TickSample {
        std::chrono::steady_clock::time_point t;    //!< arrival time
        TickArray ticks;                            //!< the line's tick counts
    };
    //! @brief Drop the samples that left the jitter windows by `now`.
    //!
    //! A window of length L holds the samples that arrived in (now - L, now].
    //! @param now Current steady_clock time.
    //! @note The caller holds m_. const because snapshot() calls it too; the
    //!       window members it updates are mutable.
    void expireJitter(std::chrono::steady_clock::time_point now) const;

    mutable std::mutex m_;          //!< guards every member below
    std::deque<ChatLine> chat_;     //!< chat log, oldest first, at most maxChatLines
    TuiSnapshot s_;                 //!< display state; snapshot() copies it and fills in
                                    //!< settleSecondsLeft and jitter in the copy
    std::chrono::steady_clock::time_point settleUntil_{};  //!< end of the base-station boot wait
    bool rateSetPending_ = false;   //!< last F sent was a set, not a query: its reply
                                    //!< means the rate was sent to the transmitter

    //! Tick lines of the last JITTER_LONG_WINDOW, oldest first; the ones from
    //! index jitterShortFirst_ on are inside JITTER_SHORT_WINDOW too. Mutable:
    //! snapshot() also expires samples, since the windows slide with time.
    mutable std::deque<TickSample> jitterSamples_;
    mutable size_t jitterShortFirst_ = 0;   //!< index of the oldest sample in the short window
    mutable WindowSums jitterLong_, jitterShort_;   //!< sums of the long / short window
};

//! @brief A request from the TUI, executed by the main loop (handleControl() in
//!        main.cpp).
struct ControlRequest {
    //! @brief What to do, and which argument (s or v) it uses
    enum class Kind {
        SendCmd,        //!< s = command line (validated commands go through Comms,
                        //!<     anything else is written raw); dropped while the
                        //!<     link is closed or the base-station is booting
        SetBaud,        //!< v = baud rate; reopens the port (host side only: the
                        //!<     base-station keeps its stored rate)
        SetPort,        //!< s = device path; reopens the port at the current baud
        SetTimeout,     //!< v = response timeout in ms
        SetMaxClients,  //!< v = TCP client cap
        SetLimitEnabled,//!< v = 0/1: disable / enable the TCP client cap
        SetLogLevel,    //!< s = spdlog level name
        ClearUdpPeers,  //!< forget the UDP stream peers
        Quit,           //!< leave the main loop and exit
    } kind;             //!< what to do
    std::string s;      //!< string argument (SendCmd, SetPort, SetLogLevel)
    long v = 0;         //!< number argument (SetBaud, SetTimeout, SetMaxClients, SetLimitEnabled)
};

//! @brief FIFO of ControlRequests: the TUI thread pushes, the main loop pops.
//! @note Thread-safe: push() and pop() take the queue's mutex.
class ControlQueue {
public:
    //! @brief Add a request at the end of the queue.
    //! @param r The request.
    void push(ControlRequest r);
    //! @brief Take the oldest request.
    //! @param r Receives it.
    //! @return false if the queue was empty (r is left unchanged).
    bool pop(ControlRequest& r);

private:
    std::mutex m_;                  //!< guards q_
    std::queue<ControlRequest> q_;  //!< pending requests, oldest first
};
