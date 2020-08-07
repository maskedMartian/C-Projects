// Small command line text editor that will run in a Linux terminal. 

// Resources for this project:
//     VT100.net VT100 User Guide, Chapter 3 Programmer Information:    https://vt100.net/docs/vt100-ug/chapter3.html
//     Wikipedia article on ANSI escape code:                           https://en.wikipedia.org/wiki/ANSI_escape_code

/*
USING COLOR:
The Wikipedia article on ANSI escape codes includes a large table containing all the different argument codes you can 
use with the m command on various terminals. It also includes the ANSI color table with the 8 foreground/background 
colors available.

The first table says we can set the text color using codes 30 to 37, and reset it to the default color using 39. The 
color table says 0 is black, 1 is red, and so on, up to 7 which is white. Putting these together, we can set the text 
color to red using 31 as an argument to the m command. After printing the digit, we use 39 as an argument to m to set 
the text color back to normal.

*/

// Small changes

/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>      // needed for iscntrl()
#include <errno.h>      // needed for errno, EAGAIN
#include <fcntl.h>      // needed for open(), O_RDWR, O_CREAT
#include <stdio.h>      // needed for perror(), printf(), sscanf(), snprintf(), FILE, fopen(), getline(), vsnprintf()
#include <stdarg.h>     // needed for va_list, va_start(), and va_end()
#include <stdlib.h>     // Needed for exit(), atexit(), realloc(), free(), malloc()
#include <string.h>     // Needed for memcpy(), strlen(), strup(), memmove(), strerror(), strstr(), memset(), strchr(), strcmp(), strncmp()
#include <sys/ioctl.h>  // Needed for struct winsize, ioctl(), TIOCGWINSZ 
#include <sys/types.h>  // Needed for ssize_t
#include <termios.h>    // Needed for struct termios, tcsetattr(), TCSAFLUSH, tcgetattr(), BRKINT, ICRNL, INPCK, ISTRIP, 
                        // IXON, OPOST, CS8, ECHO, ICANON, IEXTEN, ISIG, VMIN, VTIME
#include <time.h>       // Needed for time_t, time()
#include <unistd.h>     // Needed for read(), STDIN_FILENO, write(), STDOUT_FILENO, ftruncate(), close()

/*** defines ***/


#define TEXT_ED_VERSION    "0.0.1"
#define TAB_STOP           8
#define QUIT_TIMES         3

#define CTRL_KEY(k)        ((k) & 0x1F)  // unset the upper 3 bits of k

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

enum editorHighlight {  // enum of highlight colors
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS  (1<<0)  // For now, we define just the HL_HIGHLIGHT_NUMBERS flag bit.
#define HL_HIGHLIGHT_STRINGS  (1<<1)  // Now let’s add an HL_HIGHLIGHT_STRINGS bit flag to the flags field of the editorSyntax struct, and turn on the flag when highlighting C files.

/*** data ***/

struct editorSyntax {
  char *filetype;    // the name of the filetype that will be displayed to the user in the status bar
  char **filematch;  //  an array of strings, where each string contains a pattern to match a filename against - If the filename matches, then the file will be recognized as having that filetype
  char **keywords;  // an array of strings to hold programming language keywords
  char *singleline_comment_start;  // We’ll let each language specify its own single-line comment pattern, as they differ a lot between languages. Let’s add a singleline_comment_start string to the editorSyntax struct, and set it to "//" for the C filetype. 
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;         // a bit field that will contain flags for whether to highlight numbers and whether to highlight strings for that filetype
};

typedef struct erow {  // the typedef lets us refer to the type as "erow" instead of "struct erow"
  int size;  // the quantity of elements in the *chars array
  int rsize;  // the quantity of elements in the *render array
  char *chars;  // pointer to a dynamically allocated array that holds all the characters in a single row of text as read from a file
  char *render;  // pointer to a dynamically allocated array that holds all the characters in a single row of text as they are displayed on the screen
  unsigned char *hl;  // "highlight" - an array to store the highlighting characteristics of each character
} erow;  // erow stands for "editor row" - it stores a line of text as a pointer to the dynamically-allocated character data and a length

struct editorConfig {  // global editor state
  int cx, cy;  // cx - horizontal index into the chars field of erow (cursor location???)
  int rx;  //horizontal index into the render field of erow - if there are no tab son the current line, rx will be the same as cx 
  int rowoff;  // row offset - keeps track of what row of the file the user is currently scrolled to
  int coloff;  // column offset - keeps track of what column of the file the user is currently scrolled to
  int screenrows;  // qty of rows on the screen - window size
  int screencols;  // qty of columns on the screen - window size
  int numrows;  // the number of rows (lines) of text being displayed/stored by the editor
  erow *row;  // Hold a single row of test, both as read from a file, and as displayed on the screen
  int dirty;  // dirty flag - We call a text buffer “dirty” if it has been modified since opening or saving the file - used to keep track of whether the text loaded in our editor differs from what’s in the file
  char *filename;  // Name of the file being edited
  char statusmsg[80];  // holds an 80 character message to the user displayed on the status bar.
  time_t statusmsg_time;  // timestamp for the status message - current status message will only display for five seconds or until the next key is pressed since the screen in refreshed only when a key is pressed.
  struct editorSyntax *syntax;  // a pointer to the current editorSyntax struct in the global editor state
  struct termios orig_termios;  // structure to hold the original state of the terminal when our program began, before we started altering its state
};

struct editorConfig E;

/*** filetypes ***/

char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };  // an array of strings - must be terminated with NULL
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",

  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",  // type keywords are each ended with a | character and treated as secondary keywords
  "void|", NULL
};

struct editorSyntax HLDB[] = {  // HLDB stands for “highlight database” - it's an array of editorSyntax structs
  {
    "c",  // filetype field
    C_HL_extensions,  // filematch field
    C_HL_keywords,   // keywords field
    "//",  // singleline_comment_ start field
    "/*",
    "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS  // flags field
  },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))  // define an HLDB_ENTRIES constant to store the length of the HLDB array

/*** prototypes ***/
// 8888888b.  8888888b.   .d88888b. 88888888888 .d88888b. 88888888888 Y88b   d88P 8888888b.  8888888888 .d8888b.  
// 888   Y88b 888   Y88b d88P" "Y88b    888    d88P" "Y88b    888      Y88b d88P  888   Y88b 888       d88P  Y88b 
// 888    888 888    888 888     888    888    888     888    888       Y88o88P   888    888 888       Y88b.      
// 888   d88P 888   d88P 888     888    888    888     888    888        Y888P    888   d88P 8888888    "Y888b.   
// 8888888P"  8888888P"  888     888    888    888     888    888         888     8888888P"  888           "Y88b. 
// 888        888 T88b   888     888    888    888     888    888         888     888        888             "888 
// 888        888  T88b  Y88b. .d88P    888    Y88b. .d88P    888         888     888        888       Y88b  d88P 
// 888        888   T88b  "Y88888P"     888     "Y88888P"     888         888     888        8888888888 "Y8888P" 

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

/*** terminal ***/
// 88888888888 8888888888 8888888b.  888b     d888 8888888 888b    888        d8888 888      
//     888     888        888   Y88b 8888b   d8888   888   8888b   888       d88888 888      
//     888     888        888    888 88888b.d88888   888   88888b  888      d88P888 888      
//     888     8888888    888   d88P 888Y88888P888   888   888Y88b 888     d88P 888 888      
//     888     888        8888888P"  888 Y888P 888   888   888 Y88b888    d88P  888 888      
//     888     888        888 T88b   888  Y8P  888   888   888  Y88888   d88P   888 888      
//     888     888        888  T88b  888   "   888   888   888   Y8888  d8888888888 888      
//     888     8888888888 888   T88b 888       888 8888888 888    Y888 d88P     888 88888888 

// -----------------------------------------------------------------------------
void die(const char *s)  // prints error message and exits program with error code 1
{
  write(STDOUT_FILENO, "\x1b[2J", 4);  // clear the screen
  write(STDOUT_FILENO, "\x1b[H", 3);  // position the cursor at the top left of the screen

  perror(s);  // looks at the global errno variable and prints a secriptive error message for it preceeded by the string given to it
  exit(1);
}

// -----------------------------------------------------------------------------
void disableRawMode()  // disable raw text input mode
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {  // set the terminal's attribute flags to their original states
    die("tcsetattr");                                             // and exit program if there is an error
  }
}

// -----------------------------------------------------------------------------
void enableRawMode()  // enable raw text input mode
{
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {  // read the terminal's attribute flags into orig_terminos
    die("tcgetattr");                                  // and exit program if there is an error
  }
  atexit(disableRawMode);  // call the disableRawMode function automatically upon exit from the program form either retunr from main() 
                           // or using exit()

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);  // unset the input bitflags for enabling Ctrl-C, enabling Ctrl-M, enabling 
                                                             // parity checking, stripping the 8th bit of input, enabling Ctrl-S and Ctrl-Q
  raw.c_oflag &= ~(OPOST);  // unset the output flag for automatically prefixing a new line character with a carriage retrun character
  raw.c_cflag |= (CS8);  // set all the bits for character size to 8 bits per byte
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);  // unset the loacal bitflags for echoing to the terminal, canonical mode, enabling 
                                                    // Ctrl-V, enabling Ctrl-C and Ctrl-Z
  raw.c_cc[VMIN] = 0;  // sets the minimum number of bytes of input needed for read() to return to 0 - read can return as soon as there 
                       // is any input
  raw.c_cc[VTIME] = 1;  // sets the maximum amount of time to wait befoore read() returns to 1/10 of a second (100 milliseconds) - if 
                        // read() times out, it returns 0, which makes sense because its usual return value in the number of bytes read
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1 ) {  // set the terminal's attribute flags to those in raw
    die("tcsetattr");                                     // and exit program if there is an error
  }
}

// -----------------------------------------------------------------------------
// waits for one keypress and returns it
int editorReadKey() 
{
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {  // read 1 byte from standard input (keyboard) into c
    if (nread == -1 && errno != EAGAIN) die("read");  // and exit program if there is an error
  }

  if (c == '\x1b') {  // if c is an escape charater
    char seq[3];

    // read the next 2 bytes into seq
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';  //--- These two reads are the reason I have
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';  //--- to press the escape button 3 times???
                                                             //--- because they don't time out???
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {  // Arrow key was pressed
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1b';
  } else {
    return c;
  }
}

// -----------------------------------------------------------------------------
int getCursorPosition(int *rows, int *cols)
{
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
    return -1;
  }

  
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) {
      break;
    }
    if (buf[i] == 'R') {
      break;
    }
    i++;
  }
  buf[i] = '\0';  // terminate the string in buf with a null character

  if (buf[0] != '\x1b' || buf[1] != '[') {  // check that buf[] contains an escape sequence, if not return -1 (error)
    return -1;
  }
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {  // copy the contents of buf[2], buf[3] into rows, cols respectively 
    return -1;
  }

  return 0;
}

// -----------------------------------------------------------------------------
// get the size of the terminal window
int getWindowSize(int *rows, int *cols)
{
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, & ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12 ) {  // writes 2 escape sequence characters, the first (C command) moves the
      return -1;  // Error                                        // cursor 999 spaces to the right, the second moves it 999 spaces down
    }
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}


// 888    888 8888888 .d8888b.  888    888 888      8888888 .d8888b.  888    888 88888888888 
// 888    888   888  d88P  Y88b 888    888 888        888  d88P  Y88b 888    888     888     
// 888    888   888  888    888 888    888 888        888  888    888 888    888     888     
// 8888888888   888  888        8888888888 888        888  888        8888888888     888     
// 888    888   888  888  88888 888    888 888        888  888  88888 888    888     888     
// 888    888   888  888    888 888    888 888        888  888    888 888    888     888     
// 888    888   888  Y88b  d88P 888    888 888        888  Y88b  d88P 888    888     888     
// 888    888 8888888 "Y8888P88 888    888 88888888 8888888 "Y8888P88 888    888     888   

/*** syntax highlighting ***/

// -----------------------------------------------------------------------------
// Right now, numbers are highlighted even if they’re part of an identifier, such as the 32 in int32_t. To fix that, we’ll require that 
//numbers are preceded by a separator character, which includes whitespace or punctuation characters. We also include the null byte ('\0'),
// because then we can count the null byte at the end of each line as a separator, which will make some of our code simpler in the future.
int is_separator(int c) {  // this function should be type Boolean 
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>{}:", c) != NULL;  
  // these are ALL boolean conditions
}

// -----------------------------------------------------------------------------
// Might not need int prev_sep - maybe just use function? OR maybe keep track of what type of char the prev char was - not just separartor, but number, or other too???
void editorUpdateSyntax(erow *row) {
  row->hl = realloc(row->hl, row->rsize);  // First we realloc() the needed memory, since this might be a new row or the row might be bigger than the last time we highlighted it.
  memset(row->hl, HL_NORMAL, row->rsize);  // use memset() to set all characters to HL_NORMAL by default

  if (E.syntax == NULL) return;

  char **keywords = E.syntax->keywords;  // declare an array of strings (pointer to a pointer) and point it at the keywords array in the E.syntax struct

  // If you don’t want single-line comment highlighting for a particular filetype, you should be able to set singleline_comment_start either to NULL or to the empty string ("")
  char *scs = E.syntax->singleline_comment_start;  // We make scs an alias for E.syntax->singleline_comment_start for easier typing (and readability, perhaps?)
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;

  int scs_len = scs ? strlen(scs) : 0;  // We then set scs_len to the length of the string, or 0 if the string is NULL. This lets us use scs_len as a boolean to know whether we should highlight single-line comments
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

  // make this a boolean
  int prev_sep = 1;  // previous_separator - keeps track of whether the previous character was a separator so it can be used to recognize and highlight numbers properly. 
                     // We initialize prev_sep to 1 (meaning true) because we consider the beginning of the line to be a separator.
  int in_string = 0;  // keep track of whether we are currently inside a string
  int in_comment = 0;

  int i = 0;
  while (i < row->size) {  // loop through the characters
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;  // set to the highlight type of the previous character if not the beginning of the line

    if (scs_len && !in_string) {  // So we wrap our comment highlighting code in an if statement that checks scs_len and also makes sure we’re not in a string, since we’re placing this code above the string highlighting code (order matters a lot in this function)
      if (!strncmp(&row->render[i], scs, scs_len)) {  //  use strncmp() to check if this character is the start of a single-line comment
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);  // simply memset() the whole rest of the line with HL_COMMENT
        break;  // break out of the syntax highlighting loop
      }
    }

/*   First we add an in_comment boolean variable to keep track of whether we’re currently inside a multi-line comment (this variable isn’t used for single-line comments).
Moving down into the while loop, we require both mcs and mce to be non-NULL strings of length greater than 0 in order to turn on multi-line comment highlighting. We also check to make sure we’re not in a string, because having (/ *) inside a string doesn’t start a comment in most languages. Okay, I’ll say it: all languages.
If we’re currently in a multi-line comment, then we can safely highlight the current character with HL_MLCOMMENT. Then we check if we’re at the end of a multi-line comment by using strncmp() with mce. If so, we use memset() to highlight the whole mce string with HL_MLCOMMENT, and then we consume it. If we’re not at the end of the comment, we simply consume the current character which we already highlighted.
If we’re not currently in a multi-line comment, then we use strncmp() with mcs to check if we’re at the beginning of a multi-line comment. If so, we use memset() to highlight the whole mcs string with HL_MLCOMMENT, set in_comment to true, and consume the whole mcs string.
*/ 
    // code for highlighting /* C */ style comments
    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }

    // We will use an in_string variable to keep track of whether we are currently inside a string. If we are, then we’ll keep 
    // highlighting the current character as a string until we hit the closing quote.
    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {  // If we’re in a string and the current character is a backslash (\), and there’s at least one more character in that line that comes after the backslash, then we highlight the character that comes after the backslash with HL_STRING and consume it. We increment i by 2 to consume both characters at once.
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string) in_string = 0;  // if the string closes
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {  // if a string opens -  we highlight both double-quoted strings and single-quoted strings
          in_string = c;  // store the character so we know what the closing character will be - single or double quotes
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }


    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      // right now this will highlight all periods in 3.....4 - do I want it to do that?
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || // if the char is a number AND the previous char was a separator or a number
          (c == '.' && prev_hl == HL_NUMBER)) {                 // OR the current character is a period and the previous character was a number
        row->hl[i] = HL_NUMBER;  // set the digits to HL_NUMBER
        i++; // increment I since we are continuing the loop
        prev_sep = 0;  // reset prev_sep since the current char is not a separator
        continue;
      }
    }

    // keywords require a separator both before and after the keyword - Otherwise, the void in avoid, voided, or avoidable would be highlighted as a keyword, which is definitely a problem we want to, uh, circumnavigate.
    if (prev_sep) {  // check for previous separator character
      int j;
      for (j = 0; keywords[j]; j++) {  // for each element in the keywords array - we can use a foreach type array because the last element is holdding NULL as a value
        int klen = strlen(keywords[j]);  // length of the keyword at position j in the array
        int kw2 = keywords[j][klen - 1] == '|';  // if the last character of the keyword is a pipe 
        if (kw2) klen--;  // if the last character is a pipe, decrement length by one

        if (!strncmp(&row->render[i], keywords[j], klen) &&  // if the word in the render array matches the current keyword being checked  
            is_separator(row->render[i + klen])) {  // if the character after the keyword in the render array is a separator
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);  // use memeset to set the correct number (length of keyword) of bytes in the hl array to the correct highlight color
        i += klen;  // consume all the characters of the keyword
        break;  // end the for loop once a keyword has been found and highlighted
        }
      }
      if (keywords[j] != NULL) {  // if we have not reached the last element of the keywords array - the for loop was exited early by break
        prev_sep = 0;  // previous character is not a seperator
        continue;  // continue main while loop
      }
    }

    prev_sep = is_separator(c);  // current character is not a number - set prev_sep
    i++;  // increment i since we didn't continue the loop
  }
}  // rework this without the continue and remove the second incrementation of i

// -----------------------------------------------------------------------------
//
int editorSyntaxToColor(int hl) {
  switch (hl) {
    case HL_COMMENT: 
    case HL_MLCOMMENT: return 36; // return the ANSI code for "foreground cyan"
    case HL_KEYWORD1: return 33;  // return the ANSI code for "foreground yellow"
    case HL_KEYWORD2: return 32;  // return the ANSI code for "foreground green"
    case HL_STRING: return 35;  // return the ANSI code for "foreground magenta"
    case HL_NUMBER: return 31;  // return the ANSI code for "foreground red"
    case HL_MATCH: return 34;   // return the ANSI code for "foreground blue"
    default: return 37;         // return the ANSI code for "foreground white"
  }
}

// -----------------------------------------------------------------------------
// we loop through each editorSyntax struct in the HLDB array, and for each one of those, we loop through each pattern in its filematch 
// array. If the pattern starts with a ., then it’s a file extension pattern, and we use strcmp() to see if the filename ends with that 
// extension. If it’s not a file extension pattern, then we just check to see if the pattern exists anywhere in the filename, using 
// strstr(). If the filename matched according to those rules, then we set E.syntax to the current editorSyntax struct, and return.
void editorSelectSyntaxHighlight() {
  E.syntax = NULL;  // set E.syntax to NULL, so that if nothing matches or if there is no filename, then there is no filetype
  if (E.filename == NULL) return;

  char *ext = strrchr(E.filename, '.');  // ext = extension - strrchr() returns a pointer to the last occurrence of a character in a string (so we can look at just the file extention) - if there is no extension, then ext will be NULL

  for (unsigned int j = 0; j <HLDB_ENTRIES; j++) {  // loop  through each editorSyntax struct in the HLDB array
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {  // loop through each pattern in its filematch array
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||   // strcmp() returns 0 if two given strings are equal
          (!is_ext && strstr(E.filename, s->filematch[i]))) { 
        E.syntax = s;

        int filerow;
        for (filerow = 0; filerow < E.numrows; filerow++) {
          editorUpdateSyntax(&E.row[filerow]);
        }

        return;
      }
      i++;
    }
  }
}

/*** row operations ***/
// 8888888b.   .d88888b.  888       888       .d88888b.  8888888b.  8888888888 8888888b.         d8888 88888888888 8888888 .d88888b.  888b    888  .d8888b.  
// 888   Y88b d88P" "Y88b 888   o   888      d88P" "Y88b 888   Y88b 888        888   Y88b       d88888     888       888  d88P" "Y88b 8888b   888 d88P  Y88b 
// 888    888 888     888 888  d8b  888      888     888 888    888 888        888    888      d88P888     888       888  888     888 88888b  888 Y88b.      
// 888   d88P 888     888 888 d888b 888      888     888 888   d88P 8888888    888   d88P     d88P 888     888       888  888     888 888Y88b 888  "Y888b.   
// 8888888P"  888     888 888d88888b888      888     888 8888888P"  888        8888888P"     d88P  888     888       888  888     888 888 Y88b888     "Y88b. 
// 888 T88b   888     888 88888P Y88888      888     888 888        888        888 T88b     d88P   888     888       888  888     888 888  Y88888       "888 
// 888  T88b  Y88b. .d88P 8888P   Y8888      Y88b. .d88P 888        888        888  T88b   d8888888888     888       888  Y88b. .d88P 888   Y8888 Y88b  d88P 
// 888   T88b  "Y88888P"  888P     Y888       "Y88888P"  888        8888888888 888   T88b d88P     888     888     8888888 "Y88888P"  888    Y888  "Y8888P" 

// -----------------------------------------------------------------------------
// converts a chars index into a render index
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    rx++;
  }
  return rx;
}

// -----------------------------------------------------------------------------
// converts a render index into a chars index
// To convert an rx into a cx, we do pretty much the same thing when converting the other way: loop through the chars string, calculating the current rx value (cur_rx) as we go. But instead of stopping when we hit a particular cx value and returning cur_rx, we want to stop when cur_rx hits the given rx value and return cx
int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
    cur_rx++;

    if (cur_rx > rx) return cx;  // should handle all rx values that are valid indexes into render
  }
  return cx;  // just in case the caller provided an rx that’s out of range, which shouldn’t happen
}

// -----------------------------------------------------------------------------
// uses the chars string of an erow to fill in the contents of the render string - copy each character from chars to render
void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

// -----------------------------------------------------------------------------
// allocates memory space for a new erow, make space at any position in the E.row array, then copies the given string to it 
void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;  // validate at index is within range

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));  // allocate memory for a new row
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at)); // shift all rows after the index at down by one to make room for new row at at

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;  // why not just set it to true? - and then set to false when save file
}

// -----------------------------------------------------------------------------
//  frees memory owned by a row
void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

// -----------------------------------------------------------------------------
// editorDelRow() looks a lot like editorRowDelChar(), because in both cases we are deleting a single element from an array of elements by its index.
void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;  // validate at index
  editorFreeRow(&E.row[at]);  // free the memory owned by the row using editorFreeRow()
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));  // use memmove() to overwrite the deleted row struct with the rest of the rows that come after it
  E.numrows--;  
  E.dirty++;
}

// -----------------------------------------------------------------------------
// erow *row - pointer to an erow struct
// int at - the index at which we want to insert the character (index to insert 'at'???)
// int c - new character to insert
void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;  // validate at - Notice that at is allowed to go one character past the end of the string, in which case the character should be inserted at the end of the string.
  row->chars = realloc(row->chars, row->size + 2);  // Then we allocate one more byte for the chars of the erow (we add 2 because we also have to make room for the null byte)
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);  // use memmove() to make room for the new character. move everything from index at to the end of the string over one character leaving a "hole" at index at.  memmove() comes from <string.h>. It is like memcpy(), but is safe to use when the source and destination arrays overlap.
  row->size++;  // increment the size of the chars array, 
  row->chars[at] = c;  // assign the character to its position in the array
  editorUpdateRow(row);  // call editorUpdateRow() so that the render and rsize fields get updated with the new row content.
  E.dirty++;  // why not just set it to true? - and then set to false when save file
}

// -----------------------------------------------------------------------------
// appends a string to the end of a row
void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);  // The row’s new size is row->size + len + 1 (including the null byte), so first we allocate that much memory for row->chars
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

// -----------------------------------------------------------------------------
// erow *row - pointer to an erow struct
// int at - the index at which we want to insert the character (index to insert 'at'???)
void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/*** editor operations ***/  // This section will contain functions that we’ll call from editorProcessKeypress() when we’re mapping keypresses to various text editing operations
// 8888888888 8888888b. 8888888 88888888888 .d88888b.  8888888b.        .d88888b.  8888888b.  8888888888 8888888b.         d8888 88888888888 8888888 .d88888b.  888b    888  .d8888b.  
// 888        888  "Y88b  888       888    d88P" "Y88b 888   Y88b      d88P" "Y88b 888   Y88b 888        888   Y88b       d88888     888       888  d88P" "Y88b 8888b   888 d88P  Y88b 
// 888        888    888  888       888    888     888 888    888      888     888 888    888 888        888    888      d88P888     888       888  888     888 88888b  888 Y88b.      
// 8888888    888    888  888       888    888     888 888   d88P      888     888 888   d88P 8888888    888   d88P     d88P 888     888       888  888     888 888Y88b 888  "Y888b.   
// 888        888    888  888       888    888     888 8888888P"       888     888 8888888P"  888        8888888P"     d88P  888     888       888  888     888 888 Y88b888     "Y88b. 
// 888        888    888  888       888    888     888 888 T88b        888     888 888        888        888 T88b     d88P   888     888       888  888     888 888  Y88888       "888 
// 888        888  .d88P  888       888    Y88b. .d88P 888  T88b       Y88b. .d88P 888        888        888  T88b   d8888888888     888       888  Y88b. .d88P 888   Y8888 Y88b  d88P 
// 8888888888 8888888P" 8888888     888     "Y88888P"  888   T88b       "Y88888P"  888        8888888888 888   T88b d88P     888     888     8888888 "Y88888P"  888    Y888  "Y8888P"

// -----------------------------------------------------------------------------
// take a character and use editorRowInsertChar() to insert that character into the position that the cursor is at.
void editorInsertChar(int c) {
  if (E.cy == E.numrows) {    // if the cursor is located on the very last line of the editor text (the cursor is on the tilde line after the end of the file, so we need to append a new row)
    editorInsertRow(E.numrows, "", 0);  // allocate memory space for a new row - the new character will be inserted on a new row 
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);  // insert the character (c) into the specific row of the row array
  E.cx++;  // move the cursor forward so that the next character the user inserts will go after the character just inserted
}

// -----------------------------------------------------------------------------
//
void editorInsertNewline() {
  if (E.cx == 0) {  // if the cursor is at the beginning of a line
    editorInsertRow(E.cy, "", 0);  // insert a new blank row before the line the cursor is on
  } else {  // Otherwise, we have to split the line we’re on into two rows
    erow *row = &E.row[E.cy];  // assign a new pointer to the address of the first character of the text that will be moved down a row
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx); // insert the text pointed to by row into a new line - calls editorUpdateRow()
    row = &E.row[E.cy]; // reset row to the line that was truncated, because editorInsertRow() may have invalidated the pointer
    row->size = E.cx;  // set the size equal to the x coordinate
    row->chars[row->size] = '\0'; // add a null character to terminate the string in row->chars
    editorUpdateRow(row);  // update the row that was truncated
  }
  E.cy++;    // move the cursor to the beginning of the new row
  E.cx = 0;  // move the cursor to the beginning of the new row
}

// -----------------------------------------------------------------------------
// use editorRowDelChar() to delete a character at the position that the cursor is at.
void editorDelChar() {
  if (E.cy == E.numrows) return;  // do nothing if the cursor is on the tilde line - If the cursor’s past the end of the file, then there is nothing to delete, and we return immediately
  if (E.cx == 0 && E.cy == 0) return;  //do nothing if the cursor is at the beginning of the first line

  // Otherwise, we get the erow the cursor is on, and if there is a character to the left of the cursor, we delete it and move the cursor one to the left
  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {  // if (E.cx == 0) - if the cursor is at the beginning of the line of text, append the entire line to the previous line and reduce the size of
    E.cx = E.row[E.cy - 1].size;  // move the x coordniate of the cursor to the end of the previous row (while staying in the same row)
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);  // Append the contents of the line the cursor is on to the contents of the line above it
    editorDelRow(E.cy);  // delete the row the cursor is on
    E.cy--;  // move the cursor up one row to the position where the two lines were joined
  }
}

/*** file i/o ***/
// 8888888888 8888888 888      8888888888      8888888        d88P  .d88888b.  
// 888          888   888      888               888         d88P  d88P" "Y88b 
// 888          888   888      888               888        d88P   888     888 
// 8888888      888   888      8888888           888       d88P    888     888 
// 888          888   888      888               888      d88P     888     888 
// 888          888   888      888               888     d88P      888     888 
// 888          888   888      888               888    d88P       Y88b. .d88P 
// 888        8888888 88888888 8888888888      8888888 d88P         "Y88888P"  
                                                                                                                                                       
// -----------------------------------------------------------------------------
//
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)  // add up the lengths of each row of text, adding 1 to each one for the newline character we’ll add to the end of each line
    totlen += E.row[j].size + 1;
  *buflen = totlen;  // save the total length into buflen, to tell the caller how long the string is

  char *buf = malloc(totlen);  // allocate the required memory for the string
  char *p = buf;  // set p to point at the same address as buf - this is so we increase the address p is pointing to as we copy characters while leaving buf point to the beginning address
  for (j = 0; j < E.numrows; j++) {  // loop through the rows
    memcpy(p, E.row[j].chars, E.row[j].size);  // memcpy() the contents of each row to the end of the buffer
    p += E.row[j].size;  // advance the address being pointed at by the p pointer by the qty of characters added to the string
    *p = '\n';  // append a new line character after each row
    p++;  // advance the address being pointed at by the pointer by one to account for the newline character
  }

  return buf; // return the string - we expect the caller to free the memory
}

// -----------------------------------------------------------------------------
// for opening and reading a file from disk
void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  editorSelectSyntaxHighlight();

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' || 
                           line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;  // reset the dirty flag
}

// -----------------------------------------------------------------------------
// saves the current text on the screen to the file with error handling
void editorSave() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s (ESC x 3 to cancel)", NULL);
    if (E.filename == NULL) {  // pressing ESC causes editorPrompt to return NULL
      editorSetStatusMessage("Save aborted");
      return;
    }
    editorSelectSyntaxHighlight();
  }

  int len;
  char *buf = editorRowsToString(&len);  // converts the contents of rows array into one continuos string

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);  // create a new file if it doesn’t already exist (O_CREAT), and we want to open it for reading and writing (O_RDWR) - 0644 is the standard permissions you usually want for text files
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {  // sets the file’s size to the specified length
      if (write(fd, buf, len) == len) {  // write the contents of buf to the file referenced by fd
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/
//  .d8888b.  8888888888        d8888 8888888b.   .d8888b.  888    888 
// d88P  Y88b 888              d88888 888   Y88b d88P  Y88b 888    888 
// Y88b.      888             d88P888 888    888 888    888 888    888 
//  "Y888b.   8888888        d88P 888 888   d88P 888        8888888888 
//     "Y88b. 888           d88P  888 8888888P"  888        888    888 
//       "888 888          d88P   888 888 T88b   888    888 888    888 
// Y88b  d88P 888         d8888888888 888  T88b  Y88b  d88P 888    888 
//  "Y8888P"  8888888888 d88P     888 888   T88b  "Y8888P"  888    888 

// -----------------------------------------------------------------------------
//
void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  static int saved_hl_line;  // static variable to know which line’s hl needs to be restored
  static char *saved_hl = NULL;  // dynamically allocated array which points to NULL when there is nothing to restore

  if (saved_hl) {  // if there is something to restore
    memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);  // memcpy it to the saved line’s hl
    free(saved_hl);  // deallocate saved_hl
    saved_hl = NULL;  // set it back to NULL - so we won't have a dangling pointer
  }

  if (key == '\r' || key == '\x1b') {  // check if the user pressed Enter or Escape
    last_match = -1;  // return to defaults - no last match
    direction = 1;  // return to defaults - forward search
    return;  // we return immediately instead of doing another search
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  // Otherwise, after any other keypress, we do another search for the current query string
  if (last_match == -1) direction = 1;
  int current = last_match;  // current is the index of the current row we are searching
  int i;
  for (i = 0; i < E.numrows; i++) {  // loop through all the row structures in the E structure
    current += direction;  //  move current index forward or backward based on the value of direction
    if (current == -1) current = E.numrows - 1;  // if beginning of the text is reached, wrap around to the last row
    else if (current == E.numrows) current = 0;  // if the end of the text is reached, wrap around to the first row

    erow *row = &E.row[current];  // set a pointer to the address of E.row[i]
    char *match = strstr(row->render, query);  // searches the row structure pointed to by row->render for the first occurence of query
    if (match) {  // a match is found
      last_match = current;
      E.cy = current;  // set cursor to location of the match
      E.cx = editorRowRxToCx(row, match - row->render); // set cursor to location of the match converted from a render index to a chars index
      E.rowoff = E.numrows;  // scroll the text row where the match was found to the top of the screen -  set E.rowoff so that we are scrolled to the very bottom of the file, which will cause editorScroll() to scroll upwards at the next screen refresh so that the matching line will be at the very top of the screen
      
      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

// -----------------------------------------------------------------------------
//
void editorFind() {
  int saved_cx = E.cx;          // change all these into a cursor position struct
  int saved_cy = E.cy;          // ...
  int saved_coloff = E.coloff;  // ...
  int saved_rowoff = E.rowoff;  // ...

  char *query = editorPrompt("Search: %s (Use ESCx3/Arrows/Enter)", 
                             editorFindCallback);  // prompt for search text
  
  if (query) {  
    free(query);
  } else {  // if query is NULL, that means they pressed escape, so restore the saved values
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}


/*** append buffer ***/
//        d8888 8888888b.  8888888b.  8888888888 888b    888 8888888b.       888888b.   888     888 8888888888 8888888888 8888888888 8888888b.  
//       d88888 888   Y88b 888   Y88b 888        8888b   888 888  "Y88b      888  "88b  888     888 888        888        888        888   Y88b 
//      d88P888 888    888 888    888 888        88888b  888 888    888      888  .88P  888     888 888        888        888        888    888 
//     d88P 888 888   d88P 888   d88P 8888888    888Y88b 888 888    888      8888888K.  888     888 8888888    8888888    8888888    888   d88P 
//    d88P  888 8888888P"  8888888P"  888        888 Y88b888 888    888      888  "Y88b 888     888 888        888        888        8888888P"  
//   d88P   888 888        888        888        888  Y88888 888    888      888    888 888     888 888        888        888        888 T88b   
//  d8888888888 888        888        888        888   Y8888 888  .d88P      888   d88P Y88b. .d88P 888        888        888        888  T88b  
// d88P     888 888        888        8888888888 888    Y888 8888888P"       8888888P"   "Y88888P"  888        888        8888888888 888   T88b 


struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT { NULL, 0}

// -----------------------------------------------------------------------------
void abAppend(struct abuf *ab, const char *s, int len) 
{
  char *new = realloc(ab->b, ab->len + len);  // allocate memeory for the new string

  if (new == NULL) {
    return;
  }
  memcpy(&new[ab->len], s, len);  // copy the contents of s onto the end of the contents of new
  ab->b = new;  // update ab.b
  ab->len += len;  // update ab.len
}

// -----------------------------------------------------------------------------
void abFree(struct abuf *ab) 
{
  free(ab->b);
  // comment just so I can collapse this function
}

/*** output ***/
//  .d88888b.  888     888 88888888888 8888888b.  888     888 88888888888 
// d88P" "Y88b 888     888     888     888   Y88b 888     888     888     
// 888     888 888     888     888     888    888 888     888     888     
// 888     888 888     888     888     888   d88P 888     888     888     
// 888     888 888     888     888     8888888P"  888     888     888     
// 888     888 888     888     888     888        888     888     888     
// Y88b. .d88P Y88b. .d88P     888     888        Y88b. .d88P     888     
//  "Y88888P"   "Y88888P"      888     888         "Y88888P"      888     

// -----------------------------------------------------------------------------
//  check if the cursor has moved outside of the visible window, and if so, adjust E.rowoff so that the cursor is just inside the visible window.
void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

// -----------------------------------------------------------------------------
// draws a tilde on every row of the terminal just like Vim
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff; // To get the row of the file that we want to display at each y position, we add E.rowoff to the y position.
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Text Editor -- version %s", TEXT_ED_VERSION);
        if (welcomelen > E.screencols) {
          welcomelen = E.screencols;
        }
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome,welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];  // a pointer, hl, to the slice of the hl array that corresponds to the slice of render that we are printing
      int current_color = -1;
      int j;
      for (j = 0; j < len; j++) {  // for every character 
        if (iscntrl(c[j])) {  // We use iscntrl() to check if the current character is a control character. 
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';  // If so, we translate it into a printable character by adding its value to '@' (in ASCII, the capital letters of the alphabet come after the @ character), or using the '?' character if it’s not in the alphabetic range.
          abAppend(ab, "\x1b[7m", 4);  // use the <esc>[7m escape sequence to switch to inverted colors
          abAppend(ab, &sym, 1);  // add new symbol we just created to the buffer
          abAppend(ab, "\x1b[m", 3); //  use <esc>[m to turn off inverted colors - Unfortunately, <esc>[m turns off all text formatting, including colors. So let’s print the escape sequence for the current color afterwards.
          if (current_color != -1) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);  // clen is c length
            abAppend(ab, buf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {  // if the character gets normal highlighting
          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5);  // append an escaspe sequence for NORMAL coloring
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);  // append the character
        } else {  // if the character does not get normal highlighting 
          int color = editorSyntaxToColor(hl[j]);  // get color to highlight
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }  
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);  // after we’re done looping through all the characters and displaying them, we print a final <esc>[39m escape sequence to make sure the text color is reset to default
    }

    abAppend(ab, "\x1b[K", 3);  // append a 3-byte escape sequence which erases the line right of the cursor
                                  // "\x1b[2K" - erases whole line
                                  // "\x1b[1K" - erases line to the left of the cursor
                                  // "\x1b[0K" - erases line to the rightt of the cursor (same as "\x1b[K")
    abAppend(ab, "\r\n", 2);
  }
}

// -----------------------------------------------------------------------------
void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);   // The escape sequence <esc>[7m switches to inverted colors
                                // The m command (Select Graphic Rendition) causes the text printed after it to be printed with various 
                                // possible attributes including bold (1), underscore (4), blink (5), and inverted colors (7)
                                // For example, you could specify all of these attributes using the command <esc>[1;4;5;7m. An argument 
                                // of 0 clears all attributes, and is the default argument, so we use <esc>[m to go back to normal text formatting.
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    E.filename ? E.filename : "[No Name]", E.numrows, 
    E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    E.syntax ? E.syntax->filetype : "no filetype", E.cy + 1, E.numrows);  // prints file type (or no filetype) and current line as well as total lines
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);  // The escape sequence <esc>[m switches back to normal formatting
  abAppend(ab, "\r\n", 2);
}

// -----------------------------------------------------------------------------
void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);  // clear the message bar
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;  // if message length is grater than the width of the screen, cut it down
  if (msglen && time(NULL) - E.statusmsg_time < 5)  // display the message only if it is lenn than 5 seconds old
    abAppend(ab, E.statusmsg, msglen);
}

// -----------------------------------------------------------------------------
void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);  // set mode escape sequence - hide cursor
  // abAppend(&ab, "\x1b[2J", 4); REMOVED
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                            (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);  // reset mode escape sequence - show cursor
  
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

#if 0
// -----------------------------------------------------------------------------
// clear screen, draw tildes, and position cursor at top-left
void editorRefreshScreen()
{
  struct abuf ab = ABUF_INIT;  // initialize a new empty buffer, ab

  abAppend(&ab, "\x1b[2J", 4);    // Appends a 4-byte escape sequence (\x1b, [, 2, J ) to the buffer which will clear the screen 
                                         // \x1b is the escape character and "J" commands are used for erasing in the display 
  abAppend(&ab, "\x1b[H", 3);  // Appends a 3-byte escape sequence (\x1b, [, H ) to the buffer which will reposition the cursor
                                         // "H" commands take two parameters which default to 1 each if not specified
                                         // If you have an 80x24 size terminal \x1b[12;40H would move the cursor to the center of the screen
                                         // Escape sequence documentation: https://vt100.net/docs/vt100-ug/chapter3.html
                                         // More info on VT100: https://en.wikipedia.org/wiki/VT100    
                                         // Other terminals: https://en.wikipedia.org/wiki/Ncurses & https://en.wikipedia.org/wiki/Terminfo 
  editorDrawRows(&ab);  // append tildes

  abAppend(&ab, "\x1b[H", 3);  // append another 3-byte escape sequence (\x1b, [, H ) to the buffer which will reposition the cursor

   write(STDOUT_FILENO, ab.b, ab.len);  // wriet the contents of the buffer to the terminal
   abFree(&ab);  // deallocate the buffer memory
}
#endif

// -----------------------------------------------------------------------------
void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** input ***/
// 8888888 888b    888 8888888b.  888     888 88888888888 
//   888   8888b   888 888   Y88b 888     888     888     
//   888   88888b  888 888    888 888     888     888     
//   888   888Y88b 888 888   d88P 888     888     888     
//   888   888 Y88b888 8888888P"  888     888     888     
//   888   888  Y88888 888        888     888     888     
//   888   888   Y8888 888        Y88b. .d88P     888     
// 8888888 888    Y888 888         "Y88888P"      888       

// -----------------------------------------------------------------------------
//
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {  // The prompt is expected to be a format string containing a %s, which is where the user’s input will be displayed.
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {  // infinite loop that repeatedly sets the status message, refreshes the screen, and waits for a keypress to handle
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();  // read user input
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {  // escape key
      editorSetStatusMessage("");  // erase status message asking for a file name
      if (callback) callback(buf, c);  // the if (callback) allows the caller to pass NULL for the callback, in case they don't want to use the callback
      free(buf); // free the memory allocated to buf
      return NULL;  // end loop and return without file name
    } else if (c == '\r') {  // if Enter is pressed
      if (buflen != 0) {  // if the length of the buffer where we are storing the text is not 0 - meaning the buffer is not empty
        editorSetStatusMessage("");  // set status message back to nothing
        if (callback) callback(buf, c);  // the if (callback) allows the caller to pass NULL for the callback, in case they don't want to use the callback
        return buf;  // return the file name entered
      }
    } else if (!iscntrl(c) && c < 128) {  // Otherwise, when they input a printable character (not a control character and not a charcter value above 128 - so no characters in our editorKey enum), we append it to buf - Notice that we have to make sure the input key isn’t one of the special keys in the editorKey enum, which have high integer values. To do that, we test whether the input key is in the range of a char by making sure it is less than 128.
      if (buflen == bufsize - 1) {    // If buflen has reached the maximum capacity we allocated (stored in bufsize) 
        bufsize *= 2;                 // then we double bufsize
        buf = realloc(buf, bufsize);  // and allocate that amount of memory before appending to buf
      }
      buf[buflen++] = c;  // add the new character just entered to buf
      buf[buflen] = '\0';  // make sure it ends with a null character
    }

    if (callback) callback(buf, c);  // the if (callback) allows the caller to pass NULL for the callback, in case they don't want to use the callback
  }
}

// -----------------------------------------------------------------------------
void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy != E.numrows) {
        E.cy++;
      }
      break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

// -----------------------------------------------------------------------------
// waits for a keypress and handles it
void editorProcessKeypress() {
  static int quit_times = QUIT_TIMES;

  int c = editorReadKey();

  switch (c) {
    case '\r':  // Enter key
      editorInsertNewline();
      break;

    case CTRL_KEY('q'):  // exit program if ctrl-q is pressed
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
          "Press Ctrl-Q %d more times to quit", quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);  // clear the screen
      write(STDOUT_FILENO, "\x1b[H", 3);  // position the cursor at the top left of the screen
      exit(0);
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;

    case CTRL_KEY('f'):
      editorFind();
      break;

    case BACKSPACE:      // mapped to 127
    case CTRL_KEY('h'):  // sends the control code 8, which is originally what the Backspace character would send back in the day
    case DEL_KEY:        // mapped to <esc>[3~ (as seen in chapter 3)
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);  // pressing the right arrow and then backspace is equivalent to pressing the delete key
      editorDelChar();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows;
        }

        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;

    case CTRL_KEY('l'):  // ctrl-L is traditionally used to refresh the screen in terminal programs (we do nothing because the screen is refreshed with every keypress by default)
    case '\x1b':  // escape - we ignore escape because there are many escape sequences we aren't handling, such as F1-F12
      break;

    default:
      editorInsertChar(c);  // 7-23-2020:5:23pm - step 103 - We’ve now officially upgraded our text viewer to a text editor
      break;
  }  // END: switch (c)

  quit_times = QUIT_TIMES;
}

/*** init ***/
// 8888888 888b    888 8888888 88888888888 
//   888   8888b   888   888       888     
//   888   88888b  888   888       888     
//   888   888Y88b 888   888       888     
//   888   888 Y88b888   888       888     
//   888   888  Y88888   888       888     
//   888   888   Y8888   888       888     
// 8888888 888    Y888 8888888     888     

// -----------------------------------------------------------------------------
// Initialize all the fields in the E struct
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;  // we'll be scrolled to the top of the file by default
  E.coloff = 0;  // we'll be scrolled to the left of the file by default
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.syntax = NULL;  // When E.syntax is NULL, that means there is no filetype for the current file, and no syntax highlighting should be done

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

// -----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
  
  while (1) 
  {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
