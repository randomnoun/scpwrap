/* scpwrap.c
 * 
 * $Id$
 *
 * Wrapper for scp which translates progress text (e.g. "50%") 
 * into javascript which generates/updates a progress bar.
 *
 * Call this thing from a bash script that dumps it's output directly into an iframe 
 * which is surrounded by <script> elements.
 *
 * COMPILING
 * 
 * Remember to include the util library when compiling !
 * i.e. gcc -lutil scpwrap.c -oscpwrap
 *
 * as of 2013, you could try doing this instead, for reasons that I don't particularly understand:
 *      gcc -Wl,--no-as-needed -lutil scpwrap.c -oscpwrap
 * 
 * Modified from http://cwshep.blogspot.com/2009/06/showing-scp-progress-using-zenity.html
 *
 * Tempted to patch scp.c directly. Although scp.c will still display the progress bar if
 * stderr is redirected to a non-tty 
 * ( http://www.openbsd.org/cgi-bin/cvsweb/src/usr.bin/ssh/scp.c?rev=1.178;content-type=text%2Fx-cvsweb-markup )
 *
 * see http://www.linuxquestions.org/questions/programming-9/problem-with-child-process-658750/#post3229496
       http://www.openbsd.org/cgi-bin/cvsweb/src/usr.bin/ssh/scp.c?rev=1.178;content-type=text%2Fx-cvsweb-markup
       http://stackoverflow.com/questions/2605130/redirecting-exec-output-to-a-buffer-or-file
       http://stackoverflow.com/questions/903864/how-to-exit-a-child-process-and-return-its-status-from-execvp
 */

#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/select.h>
#include <getopt.h>
 
/* let's say that the things we're going to replace in here are:
 * %f - filename
 * %p - progress (0-100) and "%" character
 * %t - transfer size ("2112KB")
 * %s - speed ("2.1MB/s" or however progressmeter.c does things)
 * %e - ETA ("--:--" or "05:23"), hh:mm:ss, or however progressmeter.c does things)
 */

// if we get more than this number of bytes on stdout/stderr without a newline,
// then they will be emitted in >1 stdout/stderr template 
#define STDERR_BUFSIZE 1024
#define STDOUT_BUFSIZE 1024

// enabled by --js option; stdout/stderr will be javascript-String escaped
static int ESCAPE_JS = 0;

static char* TXT_STDOUT_TEMPLATE = "";
static char* TXT_STDERR_TEMPLATE = "";
static char* TXT_START_TEMPLATE = "";
static char* TXT_PROGRESS_TEMPLATE = "%p\n";
static char* TXT_END_TEMPLATE = "";

static char* JS_STDOUT_TEMPLATE = "ui.addOutput(\"%s\");\n";
static char* JS_STDERR_TEMPLATE = "ui.addOutputError(\"%s\");\n";
static char* JS_START_TEMPLATE = "var sp = ui.startScpProgress();\n";
static char* JS_PROGRESS_TEMPLATE = "sp.setProgress(\"%f\", %p, \"%t\", \"%s\", \"%e\");\n";
static char* JS_END_TEMPLATE = "ui.stopScpProgress(%c);\n";

/** convert ASCII to javascript escape.

    see http://rishida.net/tools/conversion
 */ 
void printjs (char *str) {
    int i=0;
    for (i=0; str[i]!=0; i++) {
        switch (str[i]) {
            case 0: printf("\\0"); break;
            case 8: printf("\\b"); break;
            case 9: printf("\\t"); break;
            case 10: printf("\\n"); break;
            case 13: printf("\\r"); break;
            case 11: printf("\\v"); break;
            case 12: printf("\\f"); break;
            case 34: printf("\\\""); break;
            case 39: printf("\\\'"); break;
            case 92: printf("\\\\"); break;
            default:
                if (str[i]>0x1f && str[i]<0x7F) { 
                    printf("%c", str[i]); 
                } else { 
                    printf("\\u%04x", (int) str[i]); 
                }
        }       
    }
}

/** Take a format string ("template") containing 0 or more tokens in the form 
   "{n}", and replace each token with the n'th optional parameter to this function. 
   The string after token replacement is sent to stdout. 
   
   The nparam must be set to the number of optional parameters supplied.
   All optional parameters must be of type char*.
   
   e.g. 
     printfmt(3, "first: {0}, second: {1}, third: {2}", "a", "b", "c");
   produces the output
     "first: a, second: b, third: c"

   C-like escapes ("\n", "\t" etc) in the format string will be converted into their appropriate character     
   if JS_ESCAPE is true, then values in the optional parameters will be Javascript-escaped
     (i.e. the character '\n' will be converted to the two-character "\n" escape) 
 */  
int printfmt (int nparam, const char *format, ...) {
    va_list ap;
    int i = 0;
    int inBrace = 0, inEscape = 0;
    int paramIdx = 0;
  
    char *params[nparam];
    va_start(ap, format);
    for (i=0; i<nparam; i++) { 
        params[i] = va_arg(ap, char *);
    }  
    va_end(ap);
  
    for (i=0; format[i]!=0; i++) {
        if (inEscape) {
            inEscape = 0;
            switch (format[i]) {
                case 'n': printf("\n"); break;
                case 'r': printf("\r"); break;
                case 't': printf("\t"); break;
                case '{': printf("{"); break;
                case '\\': printf("\\"); break;
                default: /* unknown escape, just print character */ printf("%c", format[i]);
            }
        } else if (inBrace) {
            switch (format[i]) {
                case '0': case '1': case '2': case '3': case '4': 
                case '5': case '6': case '7': case '8': case '9':
                    paramIdx = paramIdx*10 + (format[i]-'0');
                    break;
                case '}': 
                    inBrace = 0; 
                    if (ESCAPE_JS) { 
                        printjs(params[paramIdx]); 
                    } else { 
                        printf("%s", params[paramIdx]); 
                    }
                    break;
                default:
                    // weird thing in brace
                    return -1;
            }      
        } else {      
            switch (format[i]) {
                case '{':  paramIdx = 0; inBrace = 1; break;
                case '\\': inEscape = 1; break;
                default: printf("%c", format[i]); 
            }
        }
    }
}

/** send usage information to stdout */ 
void usage() {
    printf("usage: scpwrap [options] -- scp-options \n"
      "Where options are:\n" 
      "  --js                   use javascript default templates, and javascript-escape output strings\n"
      "  --stdoutTemplate txt   template to use for unrecognised stdout text\n"
      "  --stderrTemplate txt   template to use for unrecognised stderr text\n"
      "  --startTemplate txt    text to display before the first progressTemplate appears\n"
      "  --progressTemplate txt template to use for copy progress output\n"
      "  --endTemplate txt      template to use after copy completes\n"
      "The following placeholders can be used in progress templates:\n"
      "  %%f  filename\n"
      "  %%p  progress amount (0-100)\n"
      "  %%t  transfer size (e.g. \"2112KB\")\n"
      "  %%s  speed (e.g. \"2.1MB/s\")\n"
      "  %%e  ETA (e.g. \"--:--\" or \"05:23\")\n"
      "The following placeholder can be used in stdout/stderr templates:\n"  
      "  %%s  text string\n"
      "The following placeholder can be used in the endTemplate:\n"  
      "  %%c  exit code\n"
      "\n"
      "See the 'scpwrap' and 'scp' man page for more options. Example usage:\n"
      "  scpwrap --js -- -i identityfile user@host1:file1 user@host2:file2\n"
      );
      
    // This is a program that wraps scp and prints out the numeric progress on separate lines.\n");
    fflush(stdout);
}


/** replace all instances of a substring in a string with a relacement string.
   memory for the returned string will be reserved via malloc
   
   see http://coding.debuntu.org/c-implementing-str_replace-replace-all-occurrences-substring
 */   
char *str_replace ( const char *string, const char *substr, const char *replacement ){
    char *tok = NULL;
    char *newstr = NULL;
    char *oldstr = NULL;
    /* if either substr or replacement is NULL, duplicate string and let caller handle it */
    if ( substr == NULL || replacement == NULL ) { 
        return strdup (string); 
    }
    newstr = strdup (string);
    while ( (tok = strstr ( newstr, substr ))) {
        oldstr = newstr;
        newstr = malloc ( strlen ( oldstr ) - strlen ( substr ) + strlen ( replacement ) + 1 );
        /*failed to alloc mem, free old string and return NULL */
        if ( newstr == NULL ) {
            free (oldstr);
            return NULL;
        }
        memcpy ( newstr, oldstr, tok - oldstr );
        memcpy ( newstr + (tok - oldstr), replacement, strlen ( replacement ) );
        memcpy ( newstr + (tok - oldstr) + strlen( replacement ), tok + strlen ( substr ), strlen ( oldstr ) - strlen ( substr ) - ( tok - oldstr ) );
        memset ( newstr + strlen ( oldstr ) - strlen ( substr ) + strlen ( replacement ) , 0, 1 );
        free (oldstr);
    }
    return newstr;
}

/** main */
main(int argc, char **argv) {
    char *startTemplate = TXT_START_TEMPLATE;
    char *stdoutTemplate = TXT_STDOUT_TEMPLATE;
    char *stderrTemplate = TXT_STDERR_TEMPLATE;
    char *progressTemplate = TXT_PROGRESS_TEMPLATE;
    char *endTemplate = TXT_END_TEMPLATE;

    int shownStartTemplate = 0;       // set to 1 when startTemplate is printed
    
    char stderrBuf[STDERR_BUFSIZE];   // stderr capture buffer
    char stdoutBuf[STDOUT_BUFSIZE];   // stdout capture buffer
    char fieldBuf[STDOUT_BUFSIZE];    // as per stdout, with spaces replaced with \0x0
        
    int stderrIdx = 0, stdoutIdx = 0; // indices within stderrBuf/stdoutBuf
    ssize_t stderrc, stdoutc;         // count of bytes read into stderrBuf/stdoutBuf
    int doneStderr = 0, doneStdout=0; // set to 1 when stderr/stdout file descriptors have closed
    
    int stdoutPtyFd;                  // file descriptor for the master side of the stdout pseudoterminal    
    int stderrPipeFd[2];              // pipe used to read stderr

    pid_t pid;                        // pid of scp child process
    int exitStatus = 0;               // scp child process exist status

    // parse options
    int c;
    int digit_optind = 0;
    while (1) {
        int this_option_optind = optind ? optind : 1;
        int option_index = 0;
        static struct option long_options[] = {
            {"js",               no_argument,       0,  0 },
            {"startTemplate",    required_argument, 0,  0 },
            {"stdoutTemplate",   required_argument, 0,  0 },
            {"stderrTemplate",   required_argument, 0,  0 },
            {"progressTemplate", required_argument, 0,  0 },
            {"endTemplate",      required_argument, 0,  0 },
            {0,         0,                 0,  0 }
        };

        c = getopt_long(argc, argv, "?", long_options, &option_index);
        if (c == -1) { break; }
        switch (c) {
            case 0:
                switch (option_index) {
                    case 0: 
                        ESCAPE_JS = 1;
                        startTemplate = JS_START_TEMPLATE;
                        stdoutTemplate = JS_STDOUT_TEMPLATE;
                        stderrTemplate = JS_STDERR_TEMPLATE;
                        progressTemplate = JS_PROGRESS_TEMPLATE;
                        endTemplate = JS_END_TEMPLATE;
                        break;
                    case 1: startTemplate = optarg; break;
                    case 2: stdoutTemplate = optarg; break;
                    case 3: stderrTemplate = optarg; break;
                    case 4: progressTemplate = optarg; break;
                    case 5: endTemplate = optarg; break;
                    default:
                        fprintf(stderr, "getopt returned option_index %d\n", option_index);
                        exit(1);   
                }
                break;
            case '?':
                usage(); 
                exit(1);
                break;
            default:
                fprintf(stderr, "getopt returned character code 0%o '%c'\n", c, c);
                exit(1);
        }
    }
    if (optind >= argc) {
       fprintf(stderr, "You must supply options to 'scp' after the '--' command line-argument\n");
       usage();
       exit(1);
    }

    // replace user-specified placeholders (e.g. %s) with positional placeholders (e.g. {0})
    /* str_replace calls malloc for each result string */
    stdoutTemplate = str_replace(stdoutTemplate, "%s", "{0}");
    stderrTemplate = str_replace(stderrTemplate, "%s", "{0}");
    progressTemplate = str_replace(progressTemplate, "%f", "{0}"); // feel free to tell me that this leaks memory on each str_replace
    progressTemplate = str_replace(progressTemplate, "%p", "{1}");
    progressTemplate = str_replace(progressTemplate, "%t", "{2}");
    progressTemplate = str_replace(progressTemplate, "%s", "{3}");
    progressTemplate = str_replace(progressTemplate, "%e", "{4}");
    endTemplate = str_replace(endTemplate, "%c", "{0}");
    // TODO: check return values for NULL (malloc error)
   
    // create pipe for stderr
    pipe(stderrPipeFd);
   
    // get a pseudoterminal
    pid = forkpty (&stdoutPtyFd, NULL, NULL, NULL);
    if (pid == 0) {
        /* CHILD */
        close(stderrPipeFd[0]);    // close reading end in the child
        // dup2(stderrPipeFd[1], 1);  // send stdout to the pipe (unused; stdout is the pseudoterminal fd)
        dup2(stderrPipeFd[1], 2);  // send stderr to the pipe
        close(stderrPipeFd[1]);    // this descriptor is no longer needed

        // pass arguments "scp" then argv[optind] to argv[argc]
        // argv[optind-1] should be pointing to the '--' argument so we replace it with "scp"
        argv[optind-1] = "scp";       
        execvp("scp", &argv[optind-1]);
        perror("execvp");
        _exit (2);
    } else if (pid == -1) {
        /* ERROR */
        perror("forkpty");
        exit (1);
    } else {
        /* PARENT */
        close(stderrPipeFd[1]);  // close the write end of the pipe in the parent
        fd_set selectFds;
        int retval;

        /* Watch stdout (stdoutPtyFd) or stderr (stderrPipeFd[0]) to see when it has input. */
        FD_ZERO(&selectFds);
        FD_SET(stdoutPtyFd, &selectFds);
        FD_SET(stderrPipeFd[0], &selectFds);

        while (!(doneStdout || doneStderr)) {
            // NB: first param isn't the number of file descriptors, it's the highest file descriptor + 1
            // FD_SETSIZE may not be terribly efficient, but should work here 
            retval = select(FD_SETSIZE, &selectFds, NULL, NULL, NULL);
            if (retval==-1) { 
                perror("select()"); exit(1);
            } else {
                if (FD_ISSET(stderrPipeFd[0], &selectFds) && !doneStderr) {
                    stderrc = read(stderrPipeFd[0], &stderrBuf[stderrIdx], 1);
                    //printf ("this shouldnt be more than 1: %d, %c\n", stderrc, stderrBuf[stderrIdx]);
                    if (stderrc <= 0) {
                        doneStderr=1;
                    } else {
                        stderrIdx += stderrc; 
                        if (stderrBuf[stderrIdx-1]=='\r' || stderrBuf[stderrIdx-1]=='\n' || stderrIdx==STDERR_BUFSIZE-1) {
                            stderrBuf[stderrIdx]=0;
                            printfmt(1, stderrTemplate, stderrBuf);
                            stderrIdx=0; 
                        }
                    }  
                }
              
                if (FD_ISSET(stdoutPtyFd, &selectFds) && !doneStdout) {
                    stdoutc = read(stdoutPtyFd, &stdoutBuf[stdoutIdx], 1);
                    // printf ("this shouldnt be more than 1: %d, %c\n", stdoutc, stdoutBuf[stdoutIdx]);
                    if (stdoutc <= 0) {
                        doneStdout=1; ;
                    } else {
                        stdoutIdx += stdoutc;
                        if (stdoutBuf[stdoutIdx-1]=='\r' || stdoutBuf[stdoutIdx-1]=='\n' || stdoutIdx==STDOUT_BUFSIZE-1) {
                            stdoutBuf[stdoutIdx]=0;

                            // stdoutBuf will be something like
                            // something.tar.gz                                1% 2112KB   2.1MB/s   00:50 ETA
                            // unless we exceed BUFSIZE. which will never happen. unless we get a horrendously long filename perhaps
                    
                            // fieldBuf is set to stdoutBuf, but with spaces replaced with char(0)s
                            // fields[n] are the character positions of the nth field in fieldBuf
                            strncpy(fieldBuf, stdoutBuf, STDOUT_BUFSIZE);
                            int fieldIdx=0, fields[5], i=0;
                            for (i=0; i<stdoutIdx; i++) {
                                if (fieldBuf[i]==' ' || fieldBuf[i]=='\r') { 
                                    fieldBuf[i]=0; 
                                } else {
                                    if (fieldIdx<5 && (i==0 || fieldBuf[i-1]==0)) {
                                        fields[fieldIdx++]=i; 
                                    }
                                }
                            }
                    
                            // right number of fields and a percentage character in the right place? 
                            if (fieldIdx==5 && fieldBuf[fields[1] + strlen(&fieldBuf[fields[1]])-1]=='%') {
                                fieldBuf[fields[1] + strlen(&fieldBuf[fields[1]])-1]=0;
                                if (!shownStartTemplate) {
                                    shownStartTemplate = 1;
                                    printf("%s", startTemplate);
                                }  
                                printfmt(5, progressTemplate, &fieldBuf[fields[0]],
                                  &fieldBuf[fields[1]],&fieldBuf[fields[2]],&fieldBuf[fields[3]],&fieldBuf[fields[4]]);
                            } else {
                                // don't generate empty lines on stdout
                                if ((strncmp(stdoutBuf, "\n", STDOUT_BUFSIZE)!=0) &&
                                    (strncmp(stdoutBuf, "\r", STDOUT_BUFSIZE)!=0) ) {
                                    printfmt(1, stdoutTemplate, stdoutBuf);
                                }   
                            }
                            fflush(stdout);
                    
                            stdoutIdx=0; 
                        }
                    }  
                }  
            }
            
            // reset set
            FD_ZERO(&selectFds);
            if (!doneStdout) { FD_SET(stdoutPtyFd, &selectFds); }
            if (!doneStderr) { FD_SET(stderrPipeFd[0], &selectFds); }
        }  

        // both stdout & stderr have been closed, dump whatever's left
        if (stderrIdx!=0) { stderrBuf[stderrIdx]=0; printfmt(1, stderrTemplate, stderrBuf); }
        if (stdoutIdx!=0) { stdoutBuf[stdoutIdx]=0; printfmt(1, stdoutTemplate, stdoutBuf); }
        fflush(stdout);
        
        // get the exit status of the scp process. 
        pid_t w = waitpid (pid, &exitStatus, 0);
        if (w==-1) { perror("waitpid"); exit(1); }
        
        if (WIFEXITED(exitStatus)) {
          sprintf(stdoutBuf, "%d", WEXITSTATUS(exitStatus));
          printfmt(1, endTemplate, stdoutBuf);
          exit (WEXITSTATUS(exitStatus));  // propagate exitStatus
          
        } else if (WIFSIGNALED(exitStatus)) {
          // display a signal as -(signal number)
          sprintf(stdoutBuf, "-%d", WTERMSIG(exitStatus));
          printfmt(1, endTemplate, stdoutBuf); 
          exit (1);
            
        } else {
          // something weird
          exit (1);
        }
    }

    // this should should never be executed 
    exit (0);
} 