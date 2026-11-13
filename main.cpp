//! @file main.cpp
//! @brief UltraGpsXY ground station entry point: command-line options, the
//!        serial link to the base-station, the network server and the main loop.
//!
//! The ground station bridges the base-station Arduino (serial port, see Comms
//! and Serial) and the network (ServerModule):
//!  - TCP command port (default 9000): a client sends one base-station command
//!    ("P", "H 80", ...). The main loop validates it, sends it over serial and
//!    sends the reply line (if any) to every connected TCP client. A command
//!    that is not valid, or that arrives while the link is closed or the
//!    base-station boots, is dropped without a reply. "help" / "?" is answered
//!    by the ground station itself, to the asking client only.
//!  - UDP port (default 9001): in continuous mode ('C') the "C:" lines go to
//!    every UDP peer (an address registers by sending any non-empty datagram
//!    to the port). Latest-only: a line not sent yet is replaced by the next one.
//!  - With --tui (builds with ULTRAGPS_HAVE_TUI=1) a notcurses terminal UI
//!    shows the tick readings, the serial traffic and the settings.
//!
//! Threads: the main thread owns Comms and runs every base-station command,
//! for TCP clients and the TUI alike, one at a time. Serial, ServerModule and
//! Tui run their own threads; the TUI never touches Comms but posts
//! ControlRequests that the main loop executes (handleControl()).
//!
//! Opening the port resets the Uno through DTR, and 'B' with a valid rate (or
//! 'M' when the stored baud rate changes) makes the base-station reboot. After
//! each, nothing is sent to the board for SERIAL_SETTLE_TIME (the boot wait);
//! then the host follows a baud change and queries the board's settings again
//! (serviceLink()).

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>
#include <argparse/argparse.hpp>
#include "ServerModule/serverModule.h"
#include "CommsModule/comms.h"
#include "CommsModule/commandHelp.h"
#include "TuiModule/tuiState.h"
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
// TUI builds only (ULTRAGPS_HAVE_TUI=1, set by CMake): the notcurses UI and the
// spdlog sinks that replace the console while it runs
#if ULTRAGPS_HAVE_TUI
#include "TuiModule/tui.h"
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#endif

//! Host baud rate the serial port is opened at on start-up (--baud-rate);
//! Comms::getBaudRate() has the rate in use later
uint32_t baudRateInt;
//! Serial device opened on start-up (--serial-port); Comms::getSerialPort()
//! has the port in use later
std::string serialPort; 

//! Network device whose IPv4 address is reported (--net-device; empty string
//! means auto-detect); the server listens on every interface
std::string netDevice = "";

// Network server ports (the ServerModule itself lives in main())
unsigned int tcpPort; //!< TCP command port (--tcp-port)
unsigned int udpPort; //!< UDP port of the continuous-mode stream (--udp-port)

//! Command-line parser, filled by parseBuilder(). argparse adds -h/--help and
//! --version, which print and exit(0) inside parse_args(); "-v" is --verbose
//! here (it takes over the short form argparse gives --version).
argparse::ArgumentParser program("UltraGPS Positioning System", "1.0");

//! Quit flag: set by SIGINT/SIGTERM/SIGHUP/SIGQUIT (onQuitSignal()) and by the
//! TUI's Quit request; the main loop then exits and shuts down cleanly.
//! Comms::sendDelaySearch() polls it too, so a signal also aborts a 'D' sweep
//! (a quit from the TUI only takes effect once the sweep is over).
std::atomic<bool> g_quit{false};

//! Set when the board was reset or is rebooting (port opened or re-opened,
//! 'B'/'M' reboot): serviceLink() queries its stored settings
//! (Comms::querySettings()) once the boot wait is over and the link is open.
//! Main thread only.
std::atomic<bool> g_querySettings{false};

//! Baud rate the base-station is rebooting into after 'B' or 'M'; the host
//! switches to it once the boot wait is over (0 = none, also when the board
//! reboots at the rate already in use). Main thread only.
static long g_pendingBaud = 0;

//! True when running with --tui (set once in main(), before the TUI starts):
//! linkNote() then writes to the TUI log pane instead of the console
static bool g_tuiMode = false;

//! Time the base-station needs to boot after the port is opened (DTR reset)
//! or after it reboots itself ('B', or 'M' with a baud change). Nothing is sent
//! to the board meanwhile (TuiState::settling(); the TUI shows BOOTING).
constexpr auto SERIAL_SETTLE_TIME = std::chrono::seconds(7);

//! @brief Handler for SIGINT, SIGTERM, SIGHUP and SIGQUIT: asks the main loop
//!        to quit (the signal number is not used).
//! @note Only stores to the atomic g_quit, which is safe in a signal handler.
static void onQuitSignal(int){
    g_quit = true;
}

//! @brief Set up the command-line options on the global program parser.
//!
//! Gives each option its help text and default value, and appends the command
//! reference (cmdhelp::referenceText()) to the --help output.
//! @note Call once, before program.parse_args().
void parseBuilder(){
    //! Add command-line arguments
    program.add_argument("-v", "--verbose")
        .help("Enable verbose logging")
        .flag();

    program.add_argument("-s", "--serial-port")
        .help("Serial port to connect to (default: /dev/ttyACM0)")
        .default_value(std::string("/dev/ttyACM0"));

    //! The rates listed are those Comms::hostSupportsBaud() accepts (keep in sync)
    program.add_argument("-b", "--baud-rate")
        .help("Baud rate for the serial device: 9600, 19200, 38400, 57600 or 115200 (default: 38400)")
        .default_value(38400u)
        //! parse and check in one step: argparse's choices() gives an unclear
        //! error for a number outside the list
        .action([](const std::string& v){
            for(unsigned int b : {9600u, 19200u, 38400u, 57600u, 115200u})
                if(v == std::to_string(b)) return b;
            throw std::invalid_argument("--baud-rate " + v +
                                        ": use 9600, 19200, 38400, 57600 or 115200");
        });

    program.add_argument("-n", "--net-device")
        .help("Network interface whose IPv4 address is shown as the server address (default: auto-detect); the server listens on all interfaces")
        .default_value(std::string(""));

    program.add_argument("-t", "--tcp-port")
        .help("TCP port for communication server (default: 9000)")
        .default_value(9000u)
        .scan<'u', unsigned int>();

    program.add_argument("-u", "--udp-port")
        .help("UDP port for communcation server (default: 9001)")
        .default_value(9001u)
        .scan<'u', unsigned int>();

    //! Parsed but never read: kept so that existing command lines with -T still parse
    program.add_argument("-T", "--server-timeout")
        .help("Accepted for compatibility, currently unused (default: 5000)")
        .default_value(5000u)
        .scan<'u', unsigned int>();

    program.add_argument("-m", "--max-clients")
        .help("Maximum number of TCP clients accepted at once (default: 1)")
        .default_value(1u)
        .scan<'u', unsigned int>();

    program.add_argument("--no-client-limit")
        .help("Accept any number of TCP clients (disables --max-clients)")
        .flag();

    program.add_argument("--tui")
        .help("Run the interactive terminal UI (requires a TTY)")
        .flag();

    //! Only read with --tui; empty means defaultLogPath()
    program.add_argument("--log-file")
        .help("Log file used while the TUI is active (default: ~/.local/state/ultragps/ultragpsxy.log)")
        .default_value(std::string(""));

    //! The command reference follows the option list (without its last newline:
    //! argparse ends the epilog with a blank line itself)
    std::string reference = cmdhelp::referenceText(78);
    if(!reference.empty()) reference.pop_back();
    program.add_epilog(reference);
}

//! @brief Report a link event (reopen, baud switch, refused 'B', ...): a '*'
//!        line in the TUI log pane, or an info line on the console when
//!        headless (the TUI log is not shown then).
//! @param state TUI state that holds the log pane.
//! @param text  Message to show.
//! @note In the TUI the note only goes to the log pane, not to the log file.
static void linkNote(TuiState& state, const std::string& text){
    if(g_tuiMode) state.sys(text);
    else spdlog::info(text);
}

//! @brief Close and re-open the serial link (port and/or baud).
//!
//! Opening a USB CDC port resets the Uno via DTR, so the TUI/TCP commands wait
//! out the boot and the stored settings are queried again afterwards. Cancels a
//! pending host baud switch: the port now runs at the rate asked for. If the new
//! port or rate cannot be opened, Comms::reopen() goes back to the old one; if
//! that fails too, the link stays closed (LINK OFFLINE in the TUI).
//! @param comms Serial link to the base-station.
//! @param state TUI state: link notes and the boot wait.
//! @param port  Serial device to open.
//! @param baud  Host baud rate; callers pass only rates Comms::hostSupportsBaud()
//!              accepts.
//! @note Main thread only.
static void reopenLink(Comms& comms, TuiState& state, const std::string& port, int baud){
    g_pendingBaud = 0;
    linkNote(state, "REOPENING " + port + " @ " + std::to_string(baud));
    if(comms.reopen(port, baud) < 0){
        linkNote(state, "FAILED TO OPEN " + port + " @ " + std::to_string(baud));
        //! Still open: Comms::reopen() rolled back to the old port, and opening
        //! it reset the board too
        if(comms.isOpen()){
            state.setSettleUntil(std::chrono::steady_clock::now() + SERIAL_SETTLE_TIME);
            g_querySettings = true;
        }
        return;
    }
    linkNote(state, "PORT OPEN - WAITING FOR BASE-STATION BOOT");
    state.setSettleUntil(std::chrono::steady_clock::now() + SERIAL_SETTLE_TIME);
    g_querySettings = true;
}

//! @brief Check a rate against the ones the base-station firmware accepts for
//!        'B' (settings-eeprom.h baudRateIsValid(); keep the two lists in sync).
//! @param baud Baud rate asked for.
//! @return true if the firmware would store @p baud and reboot into it.
static bool firmwareBaudValid(long baud){
    switch(baud){
    case 300: case 600: case 1200: case 2400: case 4800: case 9600: case 14400:
    case 19200: case 28800: case 38400: case 57600: case 115200:
    case 250000: case 500000: case 1000000: return true;
    default: return false;
    }
}

//! @brief Follow a 'B' or 'M' that makes the base-station reboot at another
//!        baud rate: start the boot wait and schedule the host switch.
//!
//! 'B <baud>' with a rate the base-station accepts (any valid rate, even the
//! one in use) stores it and reboots the board into it; 'M' reboots it into
//! 38400 when the stored rate was another one. The reply goes out first, at
//! the old rate. Mark the link as booting for SERIAL_SETTLE_TIME, so neither
//! the TUI nor TCP clients send anything meanwhile; serviceLink() then switches
//! the host to the new rate and re-queries the settings. Firmware that switches
//! without rebooting is followed the same way. Runs for TUI and TCP commands;
//! any other reply is ignored.
//! @param comms Serial link (its baud rate is the host rate in use).
//! @param state TUI state: link notes and the boot wait.
//! @param cmd   Command that was sent, e.g. "B 57600" or "M".
//! @param reply Its reply line, e.g. "B: 57600" or "M: OK".
//! @note Main thread only (called by runCommand()).
static void followBaudChange(Comms& comms, TuiState& state, const std::string& cmd,
                             const std::string& reply){
    long baud = 0;
    if(reply.rfind("B:", 0) == 0){
        //! The reply shows the requested rate only when the board accepted it;
        //! otherwise the command was a query (no change, no reboot)
        long asked = 0;
        if(std::sscanf(reply.c_str() + 2, "%ld", &baud) != 1 ||
           std::sscanf(cmd.c_str() + 1, "%ld", &asked) != 1 || baud != asked) return;
    } else if(reply.rfind("M: OK", 0) == 0){
        //! The board runs at its stored rate, i.e. the host's; no change, no reboot
        if(comms.getBaudRate() == BASE_DEFAULT_BAUD) return;
        baud = BASE_DEFAULT_BAUD;
    } else {
        return;
    }
    if(!Comms::hostSupportsBaud(baud)){
        //! runCommand() refuses such a 'B'; only firmware with other rates gets here
        SPDLOG_ERROR("Base-station rebooting at {} baud, which the host does not support", baud);
        linkNote(state, "BASE-STATION REBOOTING AT " + std::to_string(baud) +
                        " BAUD - THE HOST CANNOT OPEN THAT RATE");
        return;
    }
    //! Same rate as the host's: the board still reboots, but there is nothing to switch
    g_pendingBaud = baud == comms.getBaudRate() ? 0 : baud;
    state.setSettleUntil(std::chrono::steady_clock::now() + SERIAL_SETTLE_TIME);
    g_querySettings = true;
    linkNote(state, "BASE-STATION REBOOTING AT " + std::to_string(baud) + " BAUD - WAITING " +
                    std::to_string(SERIAL_SETTLE_TIME.count()) + " S FOR THE BOOT");
}

//! @brief Finish a boot wait: follow the base-station's baud change and
//!        re-query its settings.
//!
//! Once the boot wait is over: switch the host to the rate the base-station
//! rebooted into, in place (reopening the port would pulse DTR and reboot the
//! Uno again); reopen only if that fails. Then re-query the board's settings.
//! Does nothing while the board is still booting. Called on every pass of the
//! main loop and once right after the start-up boot wait.
//! @param comms Serial link to the base-station.
//! @param state TUI state: link notes and the boot wait.
//! @note Main thread only.
static void serviceLink(Comms& comms, TuiState& state){
    if(state.settling()) return;
    if(g_pendingBaud != 0){
        const int baud = static_cast<int>(g_pendingBaud);
        g_pendingBaud = 0;
        if(comms.setBaudRate(baud) != 0){
            linkNote(state, "IN-PLACE SWITCH TO " + std::to_string(baud) + " BAUD FAILED - REOPENING");
            reopenLink(comms, state, comms.getSerialPort(), baud);
            return;     //! reopenLink() started a new boot wait (or left the link closed)
        }
        linkNote(state, "HOST SWITCHED TO " + std::to_string(baud) + " BAUD IN PLACE (NO REOPEN)");
    }
    if(g_querySettings && comms.isOpen()){
        g_querySettings = false;
        comms.querySettings();
    }
}

//! @brief Run one validated base-station command and return its response line (if any).
//!
//! Shared by TCP clients and the TUI so both follow the same Comms state
//! machine. A normal command waits for its reply line; a 'B' or 'M' that
//! reboots the base-station then starts the boot wait (followBaudChange()). A
//! 'B' with a rate the firmware accepts but the host cannot open is not sent:
//! @p res gets "ERR: B <baud> unsupported by host" instead. 'C' switches Comms
//! to continuous mode and returns once the first "C:" line arrives; 'D' runs
//! the whole delay search.
//! @param comms    Serial link to the base-station.
//! @param cleanCmd Command without trailing newline/blanks
//!                 (Comms::extractCommand()) that Comms::isValidCommand() accepts.
//! @param res      Out: the line to pass on (the reply, the final "D:" line or
//!                 the "ERR:" refusal). Not set for 'C'; only meaningful when
//!                 true is returned.
//! @param state    TUI state: link notes, the 'D' busy banner and result, the
//!                 boot wait.
//! @return true if the command got its reply (for 'C' its first line, for 'D'
//!         the end of the sweep) or was refused with an "ERR:" line; false on
//!         no reply, an aborted sweep or an invalid command.
//! @note Main thread only. Blocks until the reply arrives or the reply timeout
//!       (Comms::effectiveTimeout()) passes; a 'D' sweep blocks until it ends,
//!       with a deadline of at least 25 s (Comms::sendDelaySearch()).
static bool runCommand(Comms& comms, const std::string& cleanCmd, std::string& res,
                       TuiState& state){
    switch(comms.getCommandType(cleanCmd)){
    case Comms::CommandType::Normal:
        //! Refuse a 'B' the firmware would accept but the host cannot follow: the
        //! base-station would store it and reboot, leaving the link unreachable.
        //! Rates the firmware rejects still go through (they act as a query).
        if(cleanCmd.size() > 1 && cleanCmd[0] == 'B'){
            long baud = 0;
            if(std::sscanf(cleanCmd.c_str() + 1, "%ld", &baud) == 1
               && firmwareBaudValid(baud) && !Comms::hostSupportsBaud(baud)){
                SPDLOG_ERROR("Refusing '{}': host cannot open {} baud", cleanCmd, baud);
                linkNote(state, "REFUSED " + cleanCmd + " - HOST CANNOT OPEN " + std::to_string(baud) + " BAUD");
                res = "ERR: B " + std::to_string(baud) + " unsupported by host";
                return true;
            }
        }
        if(!comms.sendNormalCommand(cleanCmd, res)){
            SPDLOG_ERROR("No response received for normal command");
            return false;
        }
        spdlog::debug("Received SERIAL response for normal command: {}", res);
        followBaudChange(comms, state, cleanCmd, res);
        return true;
    case Comms::CommandType::Continous:
        //! Puts Comms in continuous mode (even if no first line arrives); the
        //! main loop then forwards the stream over UDP
        if(!comms.sendContinousCommand(cleanCmd)){
            SPDLOG_ERROR("Failed to send continuous command through comms module");
            return false;
        }
        SPDLOG_INFO("Sent continuous command through comms module");
        return true;
    case Comms::CommandType::DelaySearch: {
        //! Blocks for the whole sweep (the TUI shows a busy banner); g_quit aborts it
        state.setBusy("DELAY SEARCH IN PROGRESS");
        bool confirmed = false;
        bool ok = comms.sendDelaySearch(res, 25000, &g_quit, &confirmed);
        state.setBusy("");
        if(ok) state.onSearchResult(res, confirmed);
        return ok;
    }
    case Comms::CommandType::Invalid:
        break;
    }
    return false;
}

//! @brief Copy the current link settings into the TUI state.
//!
//! Port, baud rate, open or closed, continuous mode, reply timeout and spdlog
//! level, for the TUI to show.
//! @param state TUI state to update.
//! @param comms Serial link to read the settings from.
//! @note Main thread only (reads Comms).
static void publishLink(TuiState& state, const Comms& comms){
    auto lvl = spdlog::level::to_string_view(spdlog::get_level());
    state.setLink(comms.getSerialPort(), comms.getBaudRate(), comms.isOpen(),
                  comms.isContinousMode(), comms.getResponseTimeout(),
                  std::string(lvl.data(), lvl.size()));
}

//! @brief Execute one request posted by the TUI (see ControlRequest).
//!
//! SendCmd is refused (with a '*' note) while the link is closed or the
//! base-station boots. A command Comms::isValidCommand() accepts runs through
//! runCommand() like a TCP command; any other line is written raw. SetBaud and
//! SetPort reopen the port (reopenLink()); the other kinds change a host
//! setting or ask to quit. Ends by publishing the link settings (not after a
//! refused command).
//! @param r      Request to execute.
//! @param comms  Serial link to the base-station.
//! @param server Network server (client limit, UDP peers).
//! @param state  TUI state: notes, boot wait and link settings.
//! @note Main thread only: the TUI thread never touches Comms itself.
static void handleControl(const ControlRequest& r, Comms& comms, ServerModule& server,
                          TuiState& state){
    using K = ControlRequest::Kind;
    switch(r.kind){
    case K::SendCmd: {
        if(!comms.isOpen()){
            state.sys("LINK OFFLINE - command not sent: " + r.s);
            return;
        }
        if(state.settling()){
            state.sys("BASE-STATION STILL BOOTING - command not sent: " + r.s);
            return;
        }
        std::string clean = comms.extractCommand(r.s);
        if(comms.isValidCommand(clean)){
            //! The reply shows in the TUI through the RX tap; TCP clients do not get it
            std::string res;
            if(!runCommand(comms, clean, res, state) && comms.getCommandType(clean) != Comms::CommandType::Continous){
                state.sys("NO RESPONSE TO " + clean);
            }
        } else {
            //! Unknown to the host whitelist: written raw without waiting for a
            //! reply (any reply shows in the log through the RX tap). Comms is not
            //! told, so its continuous-mode flag stays as it was.
            comms.sendRaw(clean);
        }
        break;
    }
    case K::SetBaud:
    case K::SetPort: {
        //! Host side only: the base-station keeps its stored rate (see 'B')
        std::string port = r.kind == K::SetPort ? r.s : comms.getSerialPort();
        int baud = r.kind == K::SetBaud ? static_cast<int>(r.v) : comms.getBaudRate();
        reopenLink(comms, state, port, baud);
        break;
    }
    case K::SetTimeout:
        comms.setResponseTimeout(static_cast<unsigned>(r.v));
        break;
    case K::SetMaxClients:
        server.setMaxClients(static_cast<unsigned>(r.v));
        break;
    case K::SetLimitEnabled:
        server.setClientLimitEnabled(r.v != 0);
        break;
    case K::SetLogLevel:
        spdlog::set_level(spdlog::level::from_str(r.s));
        break;
    case K::ClearUdpPeers:
        server.clearUdpClients();
        break;
    case K::Quit:
        g_quit = true;
        break;
    }
    publishLink(state, comms);
}

#if ULTRAGPS_HAVE_TUI
//! @brief Default log file used while the TUI owns the terminal (--log-file
//!        not given).
//! @return $XDG_STATE_HOME/ultragps/ultragpsxy.log, else
//!         $HOME/.local/state/ultragps/ultragpsxy.log, else
//!         /tmp/ultragps/ultragpsxy.log (unset or empty variables are skipped).
//!         The directory is created if missing; errors are ignored here, and
//!         main() logs to the chat pane only if the file cannot be opened.
static std::string defaultLogPath(){
    namespace fs = std::filesystem;
    fs::path dir;
    if(const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg) dir = xdg;
    else if(const char* home = std::getenv("HOME"); home && *home) dir = fs::path(home) / ".local" / "state";
    else dir = "/tmp";
    dir /= "ultragps";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return (dir / "ultragpsxy.log").string();
}
#endif

//! @brief Entry point: parse the options, set up logging (and the TUI with
//!        --tui), open the serial link, wait for the base-station to boot,
//!        start the network server and run the main loop until asked to quit.
//!
//! Each pass of the main loop executes the TUI's requests, finishes a boot
//! wait that has ended (serviceLink()), handles at most one TCP command,
//! forwards one continuous-mode line over UDP while streaming, publishes the
//! link state to the TUI and sleeps 10 ms.
//! @param argc Argument count.
//! @param argv Arguments (see parseBuilder() or --help).
//! @return 0 after a clean shutdown; 1 for bad options, --tui without TUI
//!         support or without a terminal, or a TUI that fails to start; -1 if
//!         the serial port cannot be opened (headless only) or the server
//!         fails to start. --help and --version exit with 0 from inside the
//!         parser.
int main(int argc, char *argv[]) {

    //! Build the argument parser
    parseBuilder(); 
    //! Decode arguments
    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        //! runtime_error for unknown options, invalid_argument for numbers
        //! that do not parse and a --baud-rate outside the supported list
        std::cerr << err.what() << " (see --help)" << std::endl;
        return 1;
    }

    //! Populate variables from arguments (argparse only accepts the listed baud rates)
    serialPort = program.get<std::string>("--serial-port");
    netDevice = program.get<std::string>("--net-device");
    tcpPort = program.get<unsigned int>("--tcp-port");
    udpPort = program.get<unsigned int>("--udp-port");
    baudRateInt = program.get<unsigned int>("--baud-rate");

    const bool tuiMode = program["--tui"] == true;
#if !ULTRAGPS_HAVE_TUI
    if(tuiMode){
        std::cerr << "This build of UltraGpsXY was built without TUI support (ULTRAGPS_TUI=OFF)" << std::endl;
        return 1;
    }
#endif
    //! notcurses takes over the terminal: it needs a TTY on both stdin and stdout
    if(tuiMode && (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))){
        std::cerr << "--tui needs an interactive terminal" << std::endl;
        return 1;
    }

    //! Shared state: TUI display data plus the base-station boot (settle) timer,
    //! which headless runs use too; the ControlQueue stays empty when headless.
    TuiState state;
    ControlQueue ctl;
    g_tuiMode = tuiMode;

    //! Setup logging with spdlog
    spdlog::set_pattern("[%^%l%$] %v");
    if(program["--verbose"] == true){
        spdlog::set_level(spdlog::level::debug);
    }

#if ULTRAGPS_HAVE_TUI
    //! While the TUI owns the terminal nothing may write to stdout/stderr:
    //! route every log line into the chat pane and a log file instead.
    std::shared_ptr<spdlog::logger> previousLogger;
    std::string logPath;
    if(tuiMode){
        //! Chat pane: each log record becomes a log line (its level picks the
        //! mark and colour)
        auto cb = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [&state](const spdlog::details::log_msg& m){
                state.onLog(static_cast<int>(m.level), std::string(m.payload.data(), m.payload.size()));
            });
        std::vector<spdlog::sink_ptr> sinks{cb};
        logPath = program.get<std::string>("--log-file");
        if(logPath.empty()) logPath = defaultLogPath();
        //! Log file, truncated at start-up (true), with full timestamps
        try {
            auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath, true);
            file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            sinks.push_back(file);
        } catch(const spdlog::spdlog_ex&){
            logPath.clear();   // log only to the chat pane
        }
        //! Keep the level set above (-v); the console logger is put back on exit
        auto logger = std::make_shared<spdlog::logger>("ultragps", sinks.begin(), sinks.end());
        logger->set_level(spdlog::get_level());
        previousLogger = spdlog::default_logger();
        spdlog::set_default_logger(logger);
    }
#endif

    //! Clean exit on Ctrl-C (SIGINT), kill (SIGTERM), a terminal hang-up such
    //! as tmux kill-session (SIGHUP) and SIGQUIT
    std::signal(SIGINT, onQuitSignal);
    std::signal(SIGTERM, onQuitSignal);
    std::signal(SIGHUP, onQuitSignal);
    std::signal(SIGQUIT, onQuitSignal);

    //! Construct modules before banner so IP address is resolved for display
    Comms comms;
    ServerModule server(tcpPort, udpPort, netDevice);
    server.setMaxClients(program.get<unsigned int>("--max-clients"));
    server.setClientLimitEnabled(program["--no-client-limit"] == false);

#if ULTRAGPS_HAVE_TUI
    //! Exists in every TUI build but only starts with --tui; quitRequested()
    //! and stop() are safe on a TUI that never started
    Tui tui(state, ctl, server);
    if(tuiMode){
        //! Every line sent to / received from the base-station also goes to the
        //! TUI (chat log, tick boxes, base-station status)
        comms.setSerialTaps([&state](const std::string& l){ state.onRx(l); },
                            [&state](const std::string& l){ state.onTx(l); });
        //! Header values until publishLink() reports the real link state
        state.setLink(serialPort, static_cast<int>(baudRateInt), false, false, 500, "");
        Tui::installCrashHandlers();
        if(!tui.start()){
            //! The terminal could not be taken over: put the console logger
            //! back and report on stderr
            if(previousLogger) spdlog::set_default_logger(previousLogger);
            const char* term = std::getenv("TERM");
            std::cerr << "Failed to start the TUI (TERM=" << (term ? term : "unset")
                      << "). Try TERM=tmux-256color or TERM=xterm-256color." << std::endl;
            return 1;
        }
        state.sys("WOPR LINK ESTABLISHED. GREETINGS, PROFESSOR.");
        if(!logPath.empty()) state.sys("LOGGING TO " + logPath);
    }
    //! Quit on a signal or TUI Quit request (g_quit), or when the TUI itself
    //! asks (F10 / Ctrl-Q, or its input ended)
    auto quitRequested = [&]{ return g_quit.load() || tui.quitRequested(); };
#else
    //! Quit on a signal (g_quit)
    auto quitRequested = [&]{ return g_quit.load(); };
#endif

    //! Print ASCII art banner (the TUI shows these fields in its header and on
    //! the settings screen instead); plain "%v" lines, then the usual pattern
    if(!tuiMode){
        spdlog::set_pattern("%v");
        spdlog::info("");
        spdlog::info(" _    _ _ _              _____ _____   _____ ");
        spdlog::info("| |  | | | |            / ____|  __ \\ / ____|");
        spdlog::info("| |  | | | |_ _ __ __ _| |  __| |__) | (___  ");
        spdlog::info("| |  | | | __| '__/ _` | | |_ |  ___/ \\___ \\ ");
        spdlog::info("| |__| | | |_| | | (_| | |__| | |     ____) |");
        spdlog::info(" \\____/|_|\\__|_|  \\__,_|\\_____|_|    |_____/ ");
        spdlog::info("               Ground Station  v1.0");
        spdlog::info("");
        spdlog::info("  +-------------------------------------------------+");
        spdlog::info("  |  Serial Port  : {:<32}|", serialPort);
        spdlog::info("  |  Baud Rate    : {:<32}|", std::to_string(baudRateInt) + " bps");
        spdlog::info("  |  IP Address   : {:<32}|", server.getIP());
        spdlog::info("  |  TCP Port     : {:<32}|", tcpPort);
        spdlog::info("  |  UDP Port     : {:<32}|", udpPort);
        spdlog::info("  |  Net Device   : {:<32}|", netDevice.empty() ? "auto-detect" : netDevice);
        spdlog::info("  +-------------------------------------------------+");
        spdlog::info("");
        spdlog::set_pattern("[%^%l%$] %v");
    }

    //! Initialize Comms module (serial link to base-station Arduino)
    spdlog::info("Initializing Comms module...");
    bool linkUp = comms.begin(serialPort, baudRateInt) == 0;
    if(!linkUp){
        spdlog::error("Failed to initialize Comms module");
        //! In the TUI the user can fix the port/baud from the settings screen
        if(!tuiMode) return -1;
    } else {
        spdlog::info("Comms module initialized  [{} @ {} bps]", serialPort, baudRateInt);
    }
    publishLink(state, comms);

    if(linkUp){
        //! Wait for the base-station to boot (interruptible); a reopen requested
        //! during this wait extends state.settling()
        state.setSettleUntil(std::chrono::steady_clock::now() + SERIAL_SETTLE_TIME);
        while(!quitRequested() && state.settling()){
            //! Keep serving TUI requests during the boot wait: settings (baud,
            //! port, limits) apply at once, commands are refused until it ends
            ControlRequest req;
            while(ctl.pop(req)) handleControl(req, comms, server, state);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if(!quitRequested() && comms.isOpen()){
            //! The board is up now: learn its stored settings before the first poll
            g_querySettings = true;
            serviceLink(comms, state);
            //! First poll: the reply is not used here; with --tui it shows in
            //! the log pane through the RX tap (headless it is only logged in
            //! VERBOSE_LOGGING builds)
            std::string res;
            comms.sendNormalCommand("P", res);
        }
    }

    //! Start Server module (TCP command socket + UDP continuous stream)
    spdlog::info("Starting Server module...");
    if(!server.start()){
#if ULTRAGPS_HAVE_TUI
        //! Give the terminal and the console logger back so the error is visible
        tui.stop();
        if(previousLogger) spdlog::set_default_logger(previousLogger);
#endif
        spdlog::error("Failed to start Server module");
        return -1;
    }
    spdlog::info("Server module started  [TCP:{} | UDP:{} | IP:{}]", tcpPort, udpPort, server.getIP());

    //! Reply to "help" / "?" on the TCP port (built once, 78 columns wide)
    const std::string helpText = cmdhelp::referenceText(78);

    while(!quitRequested()){
        //! Requests from the TUI (empty when headless)
        ControlRequest req;
        while(ctl.pop(req)){
            handleControl(req, comms, server, state);
        }

        //! After a boot wait: follow a baud change, re-learn the board's settings
        serviceLink(comms, state);

        //! At most one TCP command per pass; from = id of the client that sent it
        std::string cmd;
        uint64_t from = 0;

        if(server.receiveCommand(cmd, &from)){
            spdlog::debug("Received TCP command: {}", cmd);

            if(cmdhelp::isHelpRequest(cmd)){
                //! Answered here (also while the link is booting or closed) and
                //! only to the asking client; "help" must never reach the board.
                //! Other clients would take its example lines ("M: OK", "N: R",
                //! ...) for replies.
                server.sendCommandTo(from, helpText);
            } else if(!comms.isValidCommand(cmd)){
                SPDLOG_WARN("Discarding invalid command: {}", comms.extractCommand(cmd));
            } else if(!comms.isOpen() || state.settling()){
                //! Closed link or boot wait: the command is dropped, no reply
                SPDLOG_WARN("Serial link not ready, dropping command: {}", comms.extractCommand(cmd));
            } else {
                std::string cleanCmd = comms.extractCommand(cmd);
                std::string res;
                bool ok = runCommand(comms, cleanCmd, res, state);

                //! The reply goes to every connected TCP client, not only the sender
                if(ok && !res.empty()){
                    if(!server.sendCommand(res)){
                        SPDLOG_ERROR("Failed to send response through server module");
                    } else {
                        SPDLOG_INFO("Sent response through server module");
                    }
                }
            }
        }

        //! Continuous mode: wait (up to the reply timeout) for the next "C:"
        //! line and send it to the UDP peers
        if(comms.isContinousMode() && comms.isOpen()){
            std::string res;
            if(comms.getContinousResponse(res)){
                spdlog::debug("Received SERIAL continuous data: {}", res);
                if(!server.sendContinuous(res)){
                    SPDLOG_ERROR("Failed to send continuous data through server module");
                } else {
                    SPDLOG_INFO("Sent continuous data through server module");
                }
            } else {
                SPDLOG_ERROR("Failed to get continuous response from comms module");
            }
        }

        publishLink(state, comms);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    //! Clean shutdown: give the terminal back first, then close sockets and serial
#if ULTRAGPS_HAVE_TUI
    tui.stop();
    if(previousLogger) spdlog::set_default_logger(previousLogger);
#endif
    server.stop();
    comms.end();
    spdlog::info("UltraGPS ground station stopped");
    return 0;
}
