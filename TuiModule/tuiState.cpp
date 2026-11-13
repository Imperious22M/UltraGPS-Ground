//! @file tuiState.cpp
//! @brief TuiState and ControlQueue, and the parsers for the base-station
//!        lines (tick lines, 'D' lines, numeric replies).
//!
//! The API is documented in tuiState.h.

#include "tuiState.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

bool parseTicks(const std::string& line, TickArray& out){
    if(line.size() < 3 || (line[0] != 'N' && line[0] != 'C') || line[1] != ':'){
        return false;
    }
    const char* p = line.c_str() + 2;
    TickArray tmp{};
    //! one unsigned decimal count per receiver, a comma between two of them
    for(size_t i = 0; i < TUI_RECV_CNT; ++i){
        while(*p == ' ' || *p == '\t') ++p;
        if(!std::isdigit(static_cast<unsigned char>(*p))) return false;
        char* end = nullptr;
        errno = 0;
        unsigned long v = std::strtoul(p, &end, 10);
        if(errno != 0 || v > UINT32_MAX) return false;
        tmp[i] = static_cast<uint32_t>(v);
        p = end;
        while(*p == ' ' || *p == '\t') ++p;
        if(i + 1 < TUI_RECV_CNT){
            if(*p != ',') return false;
            ++p;
        }
    }
    //! Allow trailing whitespace / CR only
    while(*p == ' ' || *p == '\t' || *p == '\r') ++p;
    if(*p != '\0') return false;
    out = tmp;
    return true;
}

bool parseDelayLine(const std::string& line, long& us, long& mean, long& valid, long& total){
    //! tail catches trailing text: only exactly four values count as a match
    char tail = 0;
    long a, b, c, d;
    if(std::sscanf(line.c_str(), "D: %ld, %ld, %ld/%ld %c", &a, &b, &c, &d, &tail) != 4) return false;
    us = a; mean = b; valid = c; total = d;
    return true;
}

namespace {
//! @brief Parse a "<letter>: <long>[, <long>[, <long>]]" reply.
//!
//! The whole line must match: the letter, a colon, one to three integers
//! separated by commas, then nothing but blanks.
//! @param line   Line received from the base-station.
//! @param letter Reply letter, e.g. 'F'.
//! @param a,b,c  Receive the first, second and third value; they may be
//!               written even when 0 is returned.
//! @return The number of values read (1-3), or 0 if the line does not match.
int parseReplyNumbers(const std::string& line, char letter, long& a, long& b, long& c){
    if(line.size() < 3 || line[0] != letter || line[1] != ':') return 0;
    //! try three values, then two, then one; tail catches trailing text
    char tail = 0;
    int n = std::sscanf(line.c_str() + 2, " %ld , %ld , %ld %c", &a, &b, &c, &tail);
    if(n == 3) return 3;
    n = std::sscanf(line.c_str() + 2, " %ld , %ld %c", &a, &b, &tail);
    if(n == 2) return 2;
    n = std::sscanf(line.c_str() + 2, " %ld %c", &a, &tail);
    return n == 1 ? 1 : 0;
}
} // namespace

void TuiState::pushChat(ChatLine line){
    //! caller holds m_
    chat_.push_back(std::move(line));
    while(chat_.size() > maxChatLines) chat_.pop_front();
}

void TuiState::onRx(const std::string& line){
    auto now = std::chrono::system_clock::now();
    TickArray ticks;
    bool isTicks = parseTicks(line, ticks);

    std::lock_guard<std::mutex> lock(m_);
    s_.rxCount++;
    pushChat({now, ChatLine::Kind::Rx, 0, line});
    if(!isTicks){
        //! Not a measurement: update BaseInfo from the replies recognised
        //! below; lastReply keeps the last one of them
        long a = 0, b = 0, c = 0, d = 0;
        BaseInfo& bi = s_.base;
        bool known = true;
        if(parseReplyNumbers(line, 'E', a, b, c) == 1){
            bi.burstDelayUs = a;
        } else if(parseReplyNumbers(line, 'H', a, b, c) == 2){
            bi.samplingMs = a;
            bi.samplingCounts = b;
        } else if(parseReplyNumbers(line, 'F', a, b, c) == 3){
            //! A new rate sets the sampling time to its maximum (80 % of the
            //! new period); the reply reports the sampling time in force, so
            //! the counts (16 us each) are recomputed when it changed
            if(bi.samplingMs != c) bi.samplingCounts = c * 1000 / 16;
            //! a valid 'F' always sends the rate to the transmitter; "F 0" is a query
            if(rateSetPending_) bi.txRateTime = now;
            rateSetPending_ = false;
            bi.triggerHz = a;
            bi.timer1Top = b;
            bi.samplingMs = c;
        } else if(parseReplyNumbers(line, 'B', a, b, c) == 1){
            bi.baud = a;
        } else if(parseDelayLine(line, a, b, c, d)){
            bi.searchSteps++;
        } else if(line.rfind("M: OK", 0) == 0){
            //! every EEPROM setting is back at its firmware default
            //! (SONIC_BURST_DELAY, SAMPLING_TIME_MS, TRIGGER_RATE_HZ, BAUD_RATE;
            //!  "H: 80, 5000", "F: 10, 6249, 80")
            bi.burstDelayUs = BASE_DEFAULT_DELAY_US;
            bi.samplingMs = BASE_DEFAULT_SAMPLING_MS;
            bi.samplingCounts = BASE_DEFAULT_SAMPLING_COUNTS;
            bi.triggerHz = BASE_DEFAULT_RATE_HZ;
            bi.timer1Top = BASE_DEFAULT_TIMER1_TOP;
            bi.baud = BASE_DEFAULT_BAUD;
            bi.txRateTime = now;   //! the default rate is sent to the transmitter
        } else if(line.rfind("N: R", 0) == 0){
            //! 'R' acknowledged: the reset packet was sent over the radio
            bi.txResetTime = now;
        } else {
            known = false;
        }
        if(known) bi.lastReply = line;
        return;
    }

    s_.lastTicks = ticks;
    s_.haveTicks = true;
    s_.lastTickTime = std::chrono::steady_clock::now();

    //! Jitter windows: the new line is inside both of them
    jitterSamples_.push_back({s_.lastTickTime, ticks});
    jitterLong_.add(ticks);
    jitterShort_.add(ticks);
    expireJitter(s_.lastTickTime);

    //! Fold into the per-second history (max and non-zero min per receiver,
    //! zeros = no echo), keyed on steady_clock so clock jumps cannot reorder it
    int64_t sec = std::chrono::duration_cast<std::chrono::seconds>(
                      s_.lastTickTime.time_since_epoch()).count();
    if(s_.history.empty() || s_.history.back().sec != sec){
        s_.history.push_back({sec, TickArray{}, TickArray{}});
    }
    auto& b = s_.history.back();
    for(size_t i = 0; i < TUI_RECV_CNT; ++i){
        if(ticks[i] > b.max[i]) b.max[i] = ticks[i];
        if(ticks[i] > 0 && (b.min[i] == 0 || ticks[i] < b.min[i])) b.min[i] = ticks[i];
    }
    //! keep the last historySeconds seconds (trimmed only when a line arrives,
    //! so the graphs skip older buckets themselves)
    while(!s_.history.empty() && s_.history.front().sec <= sec - historySeconds){
        s_.history.pop_front();
    }
}

void TuiState::onTx(const std::string& line){
    std::lock_guard<std::mutex> lock(m_);
    s_.txCount++;
    if(line == "D") s_.base.searchSteps = 0;
    //! "F <1..50>" changes the rate (and tells the transmitter); others only query
    if(!line.empty() && line[0] == 'F'){
        long hz = std::strtol(line.c_str() + 1, nullptr, 10);
        rateSetPending_ = hz >= BASE_RATE_MIN_HZ && hz <= BASE_RATE_MAX_HZ;
    }
    pushChat({std::chrono::system_clock::now(), ChatLine::Kind::Tx, 0, line});
}

void TuiState::onLog(int level, const std::string& text){
    std::lock_guard<std::mutex> lock(m_);
    pushChat({std::chrono::system_clock::now(), ChatLine::Kind::Log, level, text});
}

void TuiState::sys(const std::string& text){
    std::lock_guard<std::mutex> lock(m_);
    pushChat({std::chrono::system_clock::now(), ChatLine::Kind::Sys, 0, text});
}

void TuiState::sysLines(const std::vector<std::string>& lines){
    auto now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(m_);
    for(const auto& l : lines) pushChat({now, ChatLine::Kind::Sys, 0, l});
}

void TuiState::WindowSums::add(const TickArray& t){
    for(size_t i = 0; i < TUI_RECV_CNT; ++i){
        if(t[i] == 0) continue;     //! 0 = no echo, not a reading
        sum[i] += t[i];
        ++count[i];
    }
}

void TuiState::WindowSums::remove(const TickArray& t){
    for(size_t i = 0; i < TUI_RECV_CNT; ++i){
        //! zeros were never added (see add())
        if(t[i] == 0) continue;
        sum[i] -= t[i];
        --count[i];
    }
}

void TuiState::expireJitter(std::chrono::steady_clock::time_point now) const{
    //! A window covers (now - length, now]; samples are in arrival order
    while(jitterShortFirst_ < jitterSamples_.size() &&
          jitterSamples_[jitterShortFirst_].t <= now - JITTER_SHORT_WINDOW){
        jitterShort_.remove(jitterSamples_[jitterShortFirst_].ticks);
        ++jitterShortFirst_;
    }
    //! Anything older than the long window has left the short one above,
    //! so jitterShortFirst_ > 0 while the front is popped
    while(!jitterSamples_.empty() && jitterSamples_.front().t <= now - JITTER_LONG_WINDOW){
        jitterLong_.remove(jitterSamples_.front().ticks);
        jitterSamples_.pop_front();
        --jitterShortFirst_;
    }
}

void TuiState::setLink(const std::string& port, int baud, bool open, bool continuous,
                       unsigned timeout, const std::string& logLevel){
    std::lock_guard<std::mutex> lock(m_);
    s_.serialPort = port;
    s_.baud = baud;
    s_.linkOpen = open;
    s_.continuous = continuous;
    s_.responseTimeout = timeout;
    s_.logLevel = logLevel;
}

void TuiState::setSettleUntil(std::chrono::steady_clock::time_point t){
    std::lock_guard<std::mutex> lock(m_);
    settleUntil_ = t;
}

bool TuiState::settling() const{
    std::lock_guard<std::mutex> lock(m_);
    return std::chrono::steady_clock::now() < settleUntil_;
}

void TuiState::setBusy(const std::string& what){
    std::lock_guard<std::mutex> lock(m_);
    s_.busy = what;
}

void TuiState::onSearchResult(const std::string& line, bool confirmed){
    long us, mean, valid, total;
    if(!parseDelayLine(line, us, mean, valid, total)) return;
    std::lock_guard<std::mutex> lock(m_);
    BaseInfo& bi = s_.base;
    bi.searchResult = std::to_string(us) + " US " + std::to_string(valid) + "/" +
                      std::to_string(total) + " MEAN " + std::to_string(mean);
    if(!confirmed){
        //! may be a step line; leave searchSteps and the stored delay alone
        bi.searchResult += "  UNCONFIRMED";
        return;
    }
    //! the result line was also counted as a step by onRx()
    if(bi.searchSteps > 0) bi.searchSteps--;
    if(valid == 0){
        bi.searchResult += "  NOT STORED";
    } else {
        //! firmware stores a 0 us winner as 1 us (0 is reserved)
        bi.burstDelayUs = us ? us : 1;
    }
}

TuiSnapshot TuiState::snapshot() const{
    std::lock_guard<std::mutex> lock(m_);
    const auto now = std::chrono::steady_clock::now();
    TuiSnapshot out = s_;
    //! whole seconds left of the boot wait, rounded up
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(settleUntil_ - now).count();
    out.settleSecondsLeft = left > 0 ? static_cast<int>((left + 999) / 1000) : 0;

    //! The windows slide with time, also when no line arrives: drop what has
    //! left them by now
    expireJitter(now);
    for(size_t i = 0; i < TUI_RECV_CNT; ++i){
        //! the short window is part of the long one, so a reading in the
        //! short window means the long count is not 0 either
        if(jitterShort_.count[i] == 0){
            out.jitter[i] = -1;
            continue;
        }
        double shortMean = static_cast<double>(jitterShort_.sum[i]) / jitterShort_.count[i];
        double longMean = static_cast<double>(jitterLong_.sum[i]) / jitterLong_.count[i];
        out.jitter[i] = std::lround(std::fabs(shortMean - longMean));
    }
    return out;
}

std::deque<ChatLine> TuiState::chat(size_t count, size_t offsetFromEnd) const{
    std::lock_guard<std::mutex> lock(m_);
    std::deque<ChatLine> out;
    if(offsetFromEnd >= chat_.size()) return out;
    //! [begin, end): the last count lines before the skipped newest ones
    size_t end = chat_.size() - offsetFromEnd;
    size_t begin = end > count ? end - count : 0;
    for(size_t i = begin; i < end; ++i) out.push_back(chat_[i]);
    return out;
}

size_t TuiState::chatSize() const{
    std::lock_guard<std::mutex> lock(m_);
    return chat_.size();
}

void ControlQueue::push(ControlRequest r){
    std::lock_guard<std::mutex> lock(m_);
    q_.push(std::move(r));
}

bool ControlQueue::pop(ControlRequest& r){
    std::lock_guard<std::mutex> lock(m_);
    if(q_.empty()) return false;
    r = std::move(q_.front());
    q_.pop();
    return true;
}
