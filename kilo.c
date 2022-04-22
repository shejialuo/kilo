#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termio.h>
#include <unistd.h>

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
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

struct editorConfig {
  int cx;
  int cy;
  int screenRows;
  int screenCols;
  struct termios originalTermios;
};

struct editorConfig E;

/*
  * To deal with error
*/
void die(const char* s) {

  write(STDOUT_FILENO, "\x1b[2J",4);
  write(STDOUT_FILENO, "\x1b[H",3);

  perror(s);
  exit(1);
}

/*
  * To disable row mode when exiting
*/
void disableRawMode() {
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.originalTermios) == -1)
    die("tcsetattr");
}

/*
  * To turn off some flags of the terminal attributes
  * to enable raw mode
*/
void enableRawMode() {
  if(tcgetattr(STDIN_FILENO, &E.originalTermios) == -1) die("tcgetattr");
  struct termios raw = E.originalTermios;
  /*
    The `ECHO` feature causes each key you type to be
    printed to the terminal, so you can see what you're
    typing. This is useful in canonical mode, but really
    gets in the way when we are trying to carefully render
    a user interface in raw mode.

    You may be familiar with this mode if you've ever had to
    type a password at the terminal, when using `sudo`
    for example

    There is an `ICANON` flag that allows us to turn off
    canonical mode. This means we will finally by reading
    input *byte-byte*, instead of line-by-line.

    `ISIG` turns off `Ctrl-C` and `Ctrl-Z`.

    `IXON` disables `Ctrl-S` and `Ctrl-Q`.

    `IEXTEN` disables `Ctrl-V`.
  */
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  /*
    The terminal translates each newline `"\n"` we print
    into a carriage return followed by a newline `"\r\n"`.
    The terminal requires both of these characters in
    order to start a new line of text. The carriage return
    moves the cursor down a line, scrolling the screen if
    necessary.

    We will turn off all ouput processing features by
    turning off the `OPOST` flag.

  */
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN |ISIG);

  /*
    The `VMIN` value sets the minimum number of bytes
    of input needed before `read()` can return.

    The `VTIME` value sets the maximum amount of time
    to wait before `read()` returns. It is in tenths of
    a second

    So we can simulate the animatation.
  */
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");

  atexit(disableRawMode);
}

/*
  * To deal with the input key
*/
int editorReadKey() {
  int nread;
  char c;
  while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if(nread == -1 && errno != EAGAIN) die("read");
  }

  if(c == '\x1b') {
    char seq[3];

    if(read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if(read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if(seq[0] == '[') {
      if(seq[1] >= '0' && seq[1] <= '9') {
        if(read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if(seq[2] == '~') {
          switch(seq[1]) {
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
        switch(seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if(seq[0] == 'O') {
      switch(seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

/*
  * Get the cursor postion
*/
int getCursorPosition(int* rows, int* cols) {
  char buf[32];
  unsigned int i = 0;

  /*
    * Use `n` command to query the terminal
    * for status information. We want to give
    * it an argument to ask for position
  */
  if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while(i < sizeof(buf) - 1) {
    if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if(buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  if(buf[0] != '\x1b' || buf[1] != '[') return -1;
  if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}


/*
  * Use `ioctl` to get terminal window size. However,
  * `ioctl` isn't guaranteed to be able to request
  * the winodw size on all systems, so we are going
  * to provide a fallback method of getting the
  * window size

  * The strategy is to position the cursor at the
  * bottom-right of the screen, then use escape
  * sequences that let us query the position of
  * the cursor. That tells us how many rows and
  * columns there must be on the screen
*/
int getWindowSize(int* rows, int* cols) {
  struct winsize ws;

  if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // Move the cursor to the bottom-right
    /*
      * We are sending two secape sequences one after the other.
      * The `C` command moves the cursor to the right
      * The `B` command moves the cursor down.
      * The argument says how much to much to move it
      * right or down by, we use a so much large value `999`
    */
    if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
      return -1;
    }
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
  return -1;
}

struct appendBuf {
  char* buf;
  int length;
};

#define ABUF_INIT {NULL, 0}

/*
 * Use `realloc` to request much more memory
*/
void bufferAppend(struct appendBuf* buf, const char* s, int length) {
  char* new = realloc(buf->buf, buf->length + length);

  if(new == NULL) return;
  memcpy(&new[buf->length], s, length);
  buf->buf = new;
  buf->length += length;
}

/*
 * Free the memory
*/
void bufferFree(struct appendBuf* buf) {
  free(buf->buf);
}

/*
  * Handle drawing each row of the buffer of text
  * being edited
*/
void editorDrawRows(struct appendBuf* buf) {
  for(int y = 0; y < E.screenRows; ++y) {
    if(y == E.screenRows / 3) {
      char welcome[80];
      int welcomeLength = snprintf(welcome, sizeof(welcome),
          "Kilo editor -- version %s", KILO_VERSION);
      if(welcomeLength > E.screenCols)
        welcomeLength = E.screenCols;
      int padding = (E.screenCols - welcomeLength) / 2;
      if(padding) {
        bufferAppend(buf, "~", 1);
        padding--;
      }
      while(padding--)
        bufferAppend(buf, " ", 1);
      bufferAppend(buf, welcome, welcomeLength);
    } else {
      bufferAppend(buf, "~", 1);
    }
    /*
      * We shouldn't write `\r\n` to the last line
      * because this causes terminal to scroll in
      * order to make room for a new, blank line.
    */

    /*
     * We should clear lines at one time instead of
     * the entire screen
    */
    bufferAppend(buf, "\x1b[K", 3);

    if(y < E.screenRows - 1) {
      bufferAppend(buf, "\r\n", 2);
    }
  }
}

/*
  * To initialize the screen
*/
void editorRefreshScreen() {
  struct appendBuf buf = ABUF_INIT;
  /*
   * We use escape sequences to tell the terminal
   * to hide and show the cursor. The `h` and `l`
   * commands are used to turn on and turn off
   * various terminal features or "modes".
  */
  bufferAppend(&buf, "\x1b[?25l", 6);

  /*
    \x1b is the escape character, or 27 in decimal
    We are writing an *escape sequence* to the terminal.
    Escape sequences always start with an escape character
    followed by a `[` character.
  */

  // Move cursor position
  bufferAppend(&buf, "\x1b[H",3);

  editorDrawRows(&buf);

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  bufferAppend(&buf, buffer, strlen(buffer));

  /*
    Use <esc>[H escape sequence to reposition
  */
  // bufferAppend(&buf, "\x1b[H", 3);
  bufferAppend(&buf, "\x1b[?25h", 6);

  write(STDOUT_FILENO, buf.buf, buf.length);
  bufferFree(&buf);
}

/*
 * To process the w s a d
*/
void editorMoveCursor(int key) {
  switch (key) {
    case ARROW_LEFT:
      if(E.cx != 0) {
        E.cx--;
      }
      break;
    case ARROW_RIGHT:
      if(E.cx != E.screenCols - 1) {
        E.cx++;
      }
      break;
    case ARROW_UP:
      if(E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if(E.cy != E.screenRows - 1) {
        E.cy++;
      }
      break;
  }
}

/*
  * To process the input key
*/
void editorProcessKeypress() {
  int c = editorReadKey();

  switch(c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J",4);
      write(STDOUT_FILENO, "\x1b[H",3);
      exit(0);
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      E.cx = E.screenCols - 1;
      break;

    case PAGE_UP:
    case PAGE_DOWN: {
        int times = E.screenRows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP: ARROW_DOWN);
      }
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
  }
}

void initEditor() {
  if(getWindowSize(&E.screenRows, &E.screenCols) == -1)
    die("getWindowSize");
}

int main() {

  enableRawMode();
  initEditor();
  /*
    Now, the terminal starts in canonical mode, in this
    mode, keyboard input is only sent to your program
    when the user presses `enter`. However, we want to
    process each keypress as it comes in, so we can respond
    to it immediately.

    What we want is raw mode.
  */
  while(1) {
    /*
      This is a very useful snippet. It shows us how various
      keypresses translate into the bytes we read.

      You'll notice a few interesting things:
      + Arrow keys, `Page Up`, `Page Down`, `Home` and `End`
        all input 3 or 4 bytes to the terminal: `27`, `'['`,
        and then one or two other characters. This is known
        as an *escape sequence* All secape sequences start
        with a `27` byte. Pressing `Escape` sends a single
        `27` byte as input.
      + `Backspace` is byte `127`. `Delete` is a 4-byte
        escape sequence.
      + `Enter` is byte `10`, which is a newline character
      + The `ctrl` key combinations that do work seem to
        map the letters A-Z to the codes 1-26.
    */
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}
