//! @file commandHelp.h
//! @brief Command reference: the one description of every command the ground
//!        station accepts, i.e. the base-station commands it forwards (P, S, C,
//!        R, D, E, H, F, B, M) and the "help" / "?" request it answers itself.
//!
//! The same text is printed by "help" or "?" on the TUI command line (into the
//! log pane) and on the TCP command port, shown on the TUI HELP screen, and
//! appended to the --help output.

#pragma once

#include <string>
#include <vector>

namespace cmdhelp {

//! One line of the formatted reference
struct Line {
    //! What a line is; picks its colours on the TUI HELP screen
    enum class Kind {
        Title,      //!< section heading
        Text,       //!< paragraph text
        Command,    //!< first line of an entry: key (command syntax) + summary
        Detail,     //!< "Reply" / "Example" line of an entry
    };
    Kind kind;          //!< what the line is (picks the TUI colours)
    std::string text;   //!< the line, already wrapped, without a newline
    //! Leading columns that hold the key (command syntax, or the indent plus
    //! the "Reply"/"Example" label); the TUI draws them in another colour
    int keyCols = 0;
};

//! @brief Lays out reference lines word-wrapped to a width: titles, paragraphs
//!        and "key  text" entries.
//!
//! Below 60 columns, or for a key of KEY_COLS (13) columns or more, the key
//! goes on a line of its own and the text follows indented (stacked layout).
class Writer {
public:
    //! @param width Text width in columns (20 is the minimum used; below 60 the
    //!              stacked layout).
    explicit Writer(int width);

    //! @brief Add a section heading (wrapped like a paragraph).
    //! @param text Heading text.
    void title(const std::string& text);
    //! @brief Add a paragraph, word-wrapped to the width.
    //! @param text Paragraph text.
    void text(const std::string& text);
    //! @brief Add an empty line.
    void blank();
    //! @brief Add an entry: a key and what it does.
    //! @param key  Command syntax, e.g. "H <ms>".
    //! @param text What it does.
    void entry(const std::string& key, const std::string& text);
    //! @brief Add a labelled line under an entry, e.g. ("Reply", "H: <ms>, <counts>").
    //! @param label "Reply" or "Example".
    //! @param text  The reply format or the example.
    void detail(const std::string& label, const std::string& text);

    //! @return The lines laid out so far.
    const std::vector<Line>& lines() const { return lines_; }

private:
    //! @brief Word-wrap text (wrapText()) and append the lines as kind; only
    //!        the first line gets keyCols.
    //! @param kind    Kind of every line added.
    //! @param prefix  Start of the first line (key or label, padded).
    //! @param keyCols Key columns of the first line (Line::keyCols).
    //! @param text    Text to wrap after the prefix.
    //! @param indent  Leading spaces of the continuation lines.
    void wrap(Line::Kind kind, const std::string& prefix, int keyCols,
              const std::string& text, int indent);

    int width_;                 //!< text width in columns (at least 20)
    bool wide_;                 //!< key and text side by side (width >= 60)
    std::vector<Line> lines_;   //!< the lines laid out so far
};

//! @brief Tell whether a line asks for the command reference.
//! @param line Command line as typed or received.
//! @return true for "help" or "?" in any case, ignoring surrounding blanks and
//!         CR/LF.
bool isHelpRequest(const std::string& line);

//! @brief Word-wrap text to lines of at most width columns.
//!
//! The first line starts with prefix, the others with indent spaces. A word
//! longer than a line is cut.
//! @param prefix Start of the first line.
//! @param text   Text to wrap (split at blanks).
//! @param indent Leading spaces of the following lines.
//! @param width  Line width in columns.
//! @return At least one line (the prefix alone for an empty text).
std::vector<std::string> wrapText(const std::string& prefix, const std::string& text,
                                  int indent, int width);

//! @brief Append the command reference (base-station commands, then the ground
//!        station's own behaviour) to a Writer.
//! @param w Writer to append to.
void appendReference(Writer& w);

//! @brief The command reference as plain text (TCP reply, --help epilog).
//! @param width Text width in columns.
//! @return The reference word-wrapped to width, one newline-terminated line each.
std::string referenceText(int width = 78);

} // namespace cmdhelp
