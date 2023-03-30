/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/
#define MoTEXT_VERSION "0.0.1"
#define MoTEXT_TAB_STOP 8

#define CTRL_KEY(k) ((k)&0x1f)

enum editorKey
{
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
/*** data ***/

// global struct to store the current state of
// our editor

/*
 * typedef lets us refer to the type as erow 
 * instead of struct erow
 * 
 * erow controls the row I am going to edit
*/
typedef struct erow {
    int size;
    int rsize;  // size of the render
    char *chars;
    char *render; // contents of render.
                  // we are adding *render to display no printable character like CTRL and Tabs. 
} erow;

/*
 * editorConfig controls the global state of the editor 
*/
struct editorConfig
{
    int cx, cy; // for keeping track of cursor'x and y position
    int rx;     // Index for render field. If not tabs in current line,
                // E.rx will be same as E.cx. Else E.rx will be greater than
                // E.cx by however many extra spaces those tabs take up when rendered.
    int rowoff; // for vertical scrolling, row offset, from the beginning of the file. 
                // It determines which row of the editor's buffer 
                // is displayed at the top of the screen.
    int coloff; // symmetric implementation for horizontal scolling.
                // It determines which col(character) of the editor's buffer
                // is displayed at the left of the screen.
    int screenrows; // the number of rows in the screen 
    int screencols; // the number of rows in the screen 
    int numrows; // the number of rows in the editor's buffer.
                 // e.g. I have 20 lines in the buffer to write but 48 lines of the screen.
    erow *row; // *row is a pointer array of structs
    char *filename; // to display filename at the status bar.
    char statusmsg[80]; // to display prompts for user input when doing a search, etc..
    time_t statusmsg_time; // timestamp to erase the statusmsg a few seconds after it's displayed.
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

void die(const char *s)
{
    // if error happens, clear the screen first
    // then print the error message
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    if (c == '\x1b')
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
        }
        else if (seq[0] == '0')
        {
            switch (seq[1])
            {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }
    else
    {
        return c;
    }
}
/*
    It looks like it's setting and cols just by
    looking at the function parameters.

    But actually the function returns ws's col and ws's row
    values to input params' pointers.
    In this way, you actually GET the values of rows and cols of
    a terminal's size.

    On success, we get 0. On failed, we get -1.

*/

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    // n is for quering the terminal for status information
    // 6 is for asking the cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i ++;
    }
    // make sure the end of the string is goddamn fucking '\0'
    buf[i] = '\0';

    // the first byte of char *buf is an escape character,
    // so it wont display on the terminal.
    // printf("\r\n&buf[1]: '%s'\r\n", &buf[1]);

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;

        return getCursorPosition(rows, cols);
    }
    else
    {
        // common approach to having functions
        // return multiple values
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/

int editorRowCxToRx(erow *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j ++)
    {
        if (row->chars[j] == '\t') 
            // a magic formula that just works lol.
            rx += (MoTEXT_TAB_STOP - 1) - (rx % MoTEXT_TAB_STOP);
        rx ++;
    }
    return rx;
}

void editorUpdateRow(erow *row)
{
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j ++) 
        if(row->chars[j] == '\t') tabs ++;
    
    free(row->render);
    // '\t' already takes up 1 byte. 
    // So we need another 7 bytes for each tab. 
    row->render = malloc(row->size + tabs * (MoTEXT_TAB_STOP - 1) + 1);


    int idx = 0;
    for(j = 0; j < row->size; j ++) 
    {
        if (row->chars[j] == '\t')
        {
            row->render[idx ++] = ' ';
            while (idx % MoTEXT_TAB_STOP != 0) row->render[idx ++] = ' '; 
        }
        else row->render[idx ++] = row->chars[j];
    }

    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) 
{
    // multiply by the number of bytes that each erow takes
    // and the number of the rows we want.
    // E.numrows is the number of rows in the editor's buffer
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    // set 'at' index to the new row we want to initialize
    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows ++;

}

/*** file i/o ***/

/**
 *
 *  First, we pass it a null line pointer and a linecap (line capacity) of 0. 
 *  That makes it allocate new memory for the next line it reads, 
 *  and set line to point to the memory, 
 *  and set linecap to let you know how much memory it allocated. 
 *  Its return value is the length of the line it read, 
 *  or -1 if it’s at the end of the file and there are no more lines to read. 
*/
void editorOpen(char *filename) 
{
    free(E.filename);
    E.filename = strdup(filename);
    // Open the file in read mode
    FILE *fp = fopen(filename, "r");
    // If the file pointer is null, exit the program with an error message
    if (!fp) die("fopen");

    // Initialize variables for reading the file line by line
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;


    // Read the file line by line until it reaches end of file
    while((linelen = getline(&line, &linecap, fp)) != -1)
    {
        // Remove any trailing newline or carriage return characters
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen --;
        editorAppendRow(line, linelen);
    }

    // Free the memory allocated for the line variable
    free(line);

    // Close the file
    fclose(fp);
}


/*** append buffer ***/

struct abuf
{
    char *b;
    int len;
};

// {NULL, 0} is a struct. LOL!
#define ABUF_INIT {NULL, 0};

/*

This function appends a string s of length len to a dynamic buffer
represented by the abuf struct pointed to by ab.

The function first attempts to resize the buffer to accommodate the new string using the realloc() function,
which returns a pointer to the newly resized buffer. If realloc() returns NULL,
the function returns without modifying the buffer.

If the buffer was successfully resized, the function then uses the memcpy() function to copy the contents
of the input string s to the end of the buffer,  starting at the current length of the buffer ab->len.
Finally, the function updates the ab->b pointer to point to the newly resized buffer
and increments the ab->len variable to reflect the new length of the buffer

*/
void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

/*** output ***/
void editorScroll()
{
    E.rx = 0;
    if (E.cy < E.numrows) E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    // if the cursor is above the top of the screen, adjust the rowoff variable
    // to scroll the screen up.

    // E.rowoff refers to what's at the top of the screen.
    if (E.cy < E.rowoff) E.rowoff = E.cy; // cy is the y position of current cursor in the FILE
    if (E.cy >= E.rowoff + E.screenrows) E.rowoff = E.cy - E.screenrows + 1; 
    
    /*
        e.g : file has 48 lines, screen columns is 30 lines.
        => numrows = 48, E.screenrows = 30, E.rowoff = 0.

        Let's say now E.cy = 30. Before we attempt to do any scrolling.

        If we press DOWN ARROW key ONCE:
        (1)editorProcessKey() is called.
        (2)editorRefreshScreen() is called.
        (3)editorSroll() is called:
            we have: 
            E.cy = 31 > E.rowoff = 0, so now E.rowoff = E.cy - E.screenrows + 1 = 31 - 30 + 1 = 1.
        (4)editorDrawRows() is called to draw the new visualization of the text editor.
            loop through y, where   0 <= y <= E.screenrows = 30
            Therefore, filerow will be in ranged: 1 <= filerow + y <= 31, which is range of the file
            that will be displayed on the screen.

    */

    if (E.rx < E.coloff) E.coloff = E.rx;
    if (E.rx >= E.coloff + E.screencols) E.coloff = E.rx - E.screencols + 1;

}

void editorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        // y is the current row in the screen, E.rowoff is the offset from y.
        // so filerow = y + E.rowoff is the index in the editor's buffer.
        // namely, the row number of cursor in the file being edited.
        int filerow = y + E.rowoff;

        // if filerow, the index is greater or equals to 
        // number of rows in the editor's buffer, that means that
        // row is empty, and the row should be written out a '~'.
        if (filerow >= E.numrows)
        {
            // only show welcome message when not opening a file
            if (E.numrows == 0 && y == E.screenrows / 3)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                                        "MoText -- version %s", MoTEXT_VERSION);
                if (welcomelen > E.screencols) welcomelen = E.screencols;
                // To center a string, you divide the screen width by 2,
                // and then subtract half of the string’s length from that
                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }

            else
            {
                abAppend(ab, "~", 1);
            }
        }
        else
        {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }
        // K command erases part of the current line.
        // 0 here erases the part of the line right of the cursor.
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
       
    }
}

void editorDrawStatusBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
        E.filename ? E.filename : "[No Name]", E.numrows);
    /*
        The current line is stored in E.cy, which we add 1 to 
        since E.cy is 0-indexed. 

        After printing the first status string, keep printing spaces 
        until we get to the point where if we printed the second status string, 
        it would end up against the right edge of the screen. 

        That happens when E.screencols - len is equal to the length of the second status string. 
        At that point we print the status string and break out of the loop, 
        as the entire status bar has now been printed.
    */
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
        E.cy + 1, E.numrows);
    // if the window is too small(narrow) that 
    // can't take 80 bytes, we truncate it.
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);

    while (len < E.screencols)
    {   
        if (E.screencols - len == rlen)
        {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len ++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen()
{
    /*
    \x1b is the escape charcter. It's 27 in decimal.
    I am writing 4 bytes out to the terminal
    escape character is always followed by a [ character

    Escape sequences instruct the terminal to do various text
    like formatting tasks, such as coloring text,
    moving the cursor around, and clearing parts of the screen.

    The J command is to clear the screen.
    The 2 is an argument before the command, which says clear
    the entire screen. 1 would clear the screen up to where
    the cursor is. 0 is the default argument for J, which would
    clear the screen from the cursor up to the end of the screen.
    */
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // this is for hiding the cursor before refreshing the screen
    abAppend(&ab, "\x1b[H", 3); // to move the cursor to the top-left corner of the screen

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);
  
    char buf[32];
    // E.cx and E.cy refer to the cursor position in the file.
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                              (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // this is for showing the cursor immediately when the refresh is done.

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** input ***/

void editorMoveCursor(int key)
{
    // To limit user to point to the only valid positions in the file
    // e.g. no 30 lines after the EOF. Only 1 line is acceptable because
    // we may need to insert new line. Same logic applies to row also.
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0) E.cx--;
        else if (E.cy > 0)
        {

            /* 
                allow user to press left to go back to the end of previous line
                E.cy > 0 is to make sure current line is not the first line. 
            */             
            E.cy --;
            E.cx = E.row[E.cy].size;
        }
        break;
    case ARROW_RIGHT:
        if (row && E.cx < row->size) E.cx++; // when E.cx is at row->size, it stops.
                                             // This is actually at the '\0' of the row. Genius.
        else if (row && E.cx == row->size) 
        {
            /* 
                allow user to press right to go to the beginning of next line
                if(row) is to make sure we are not at the end of the file.
            */             
            E.cy ++;
            E.cx = 0;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0) E.cy--;
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows) E.cy++; // so E.cy will not go past the end of the file!
        break;
    }

    /*
        This is for snapping the cursor jump. Basically, we dont want this to happen:
        line 1: --------------------------------*(before cursor)*
        line 2: --------------------            *(after cursor)*

        The cursor could jump from the end of a longer line to a shorter line, violating
        our rules!

        So when a cursor does that jump, we snap away the line overhead. 
    */

    // row has to be reset because E.cy could point to a different line than it did before.
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;

    // set E.cx to the end of that line if E.cx is to the right of the end of that line.
    if (E.cx > rowlen) E.cx = rowlen;
}

void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case HOME_KEY:
        E.cx = 0;
        break;

    case END_KEY:
        if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
        break;

    case PAGE_UP:
    case PAGE_DOWN:
        {
            if (c == PAGE_UP) E.cy = E.rowoff;
            else if (c == PAGE_DOWN)
            {
                E.cy = E.rowoff + E.screenrows - 1;
                if (E.cy > E.numrows) E.cy = E.numrows;
            }

            int times = E.screenrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            break;

        }
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
    }
}

/*** init ***/

void initEditor()
{
    // initialize x and y to be both 0 so they can start
    // at the top left corner of the screen.
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    // initialize it to 0, which means we'll be scrolled 
    // by default to the top of file
    E.rowoff = 0; 
    E.coloff = 0; 
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    // when you pass in E.screenrows and &E.screencols
    // it actually set the values for them, hence "init".
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2;
}

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc >= 2)
    {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-Q = quit");

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}