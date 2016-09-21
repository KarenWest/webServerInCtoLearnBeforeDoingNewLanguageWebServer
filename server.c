// Karen West - done in early April 2016 - downloaded one of the assignments
//from EdX Harvard's CS50 course - did not do entire course, just tried this.
//
// server.c
//
// Computer Science 50
// Problem Set 6
//
// Server is started with: ./server public
// under the public dir is the test/index.html file
//
//To run server (after you compile it with makefile):
//./server public 
//(gives public directory as root to server--the test dir is below it)
// Tests: in client browser while this server is running on same machine:
//tests that work:
//http://localhost:8080/test/index.html (runs a you tube video)
//http://localhost:8080/cat.jpg (photo)
//http://localhost:8080/cat.html
//test that does not work - php! will incorporate later:
//http://localhost:8080/hello.php

// feature test macro requirements
#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED

// limits on an HTTP request's size, based on Apache's
// http://httpd.apache.org/docs/2.2/mod/core.html
#define LimitRequestFields 50
#define LimitRequestFieldSize 4094
#define LimitRequestLine 8190

char pathChars[LimitRequestLine + 1];
char pathCopy[LimitRequestLine + 1];

// number of bytes for buffers
#define BYTES 512

// header files
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "server.h"

// types
typedef char BYTE;

// prototypes
bool connected(void);
void error(unsigned short code);
void freedir(struct dirent** namelist, int n);
void handler(int signal);
char* htmlspecialchars(const char* s);
char* indexes(const char* path);
void interpret(const char* path, const char* query);
void list(const char* path);
bool load(FILE* file, BYTE** content, size_t* length);
const char* lookup(const char* path);
bool parse(const char* line, char* path, char* query);
const char* reason(unsigned short code);
void redirect(const char* uri);
bool request(char** message, size_t* length);
void respond(int code, const char* headers, const char* body, size_t length);
void start(short port, const char* path);
void stop(void);
void transfer(const char* path, const char* type);
char* urldecode(const char* s);

// server's root
char* root = NULL;

// file descriptor for sockets
int cfd = -1, sfd = -1;

// flag indicating whether control-c has been heard
bool signaled = false;

int main(int argc, char* argv[])
{
    // a global variable defined in errno.h that's "set by system 
    // calls and some library functions [to a nonzero value]
    // in the event of an error to indicate what went wrong"
    errno = 0;

    // default to port 8080
    int port = 8080;

    // usage
    const char* usage = "Usage: server [-p port] /path/to/root";

    // parse command-line arguments
    int opt;
    while ((opt = getopt(argc, argv, "hp:")) != -1)
    {
        switch (opt)
        {
            // -h
            case 'h':
                printf("%s\n", usage);
                return 0;

            // -p port
            case 'p':
                port = atoi(optarg);
                break;
        }
    }

    // ensure port is a non-negative short and path to server's root is specified
    if (port < 0 || port > SHRT_MAX || argv[optind] == NULL || strlen(argv[optind]) == 0)
    {
        // announce usage
        printf("%s\n", usage);

        // return 2 just like bash's builtins
        return 2;
    }

    // start server
    start(port, argv[optind]);

    // listen for SIGINT (aka control-c)
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT, &act, NULL);

    // a message and its length
    char* message = NULL;
    size_t length = 0;

    // path requested
    char* path = NULL;
    printf("checking for client connections\n");
    // accept connections one at a time
    while (true)
    {
      printf("starting infinite loop to look for client browser requests\n");
        // free last path, if any
        if (path != NULL)
        {
            free(path);
            path = NULL;
        }

        // free last message, if any
        if (message != NULL)
        {
            free(message);
            message = NULL;
        }
        length = 0;

        // close last client's socket, if any
        if (cfd != -1)
        {
            close(cfd);
            cfd = -1;
        }

        // check for control-c
        if (signaled)
        {
            stop();
        }
	printf("checking if client has connected\n");
        // check whether client has connected
        if (connected())
        {
	  printf("client has connected - check for request\n");
            // check for request
            if (request(&message, &length))
            {
	      printf("message from client = %s length = %d\n", message, length);
                // extract message's request-line
                // http://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html
                const char* haystack = message;
                const char* needle = strstr(haystack, "\r\n");
                if (needle == NULL)
                {
                    error(500);
                    continue;
                }
                char line[needle - haystack + 2 + 1];
                strncpy(line, haystack, needle - haystack + 2);
                line[needle - haystack + 2] = '\0';

                // log request-line
                printf("%s", line);

                // parse request-line
                char abs_path[LimitRequestLine + 1];
                char query[LimitRequestLine + 1];
		printf("parsing request-line from client: line = %s\n", line);
                if (parse(line, abs_path, query))
                {
		  printf("abs_path returned from parse = %s\n", abs_path);
		  printf("query returned from parse = %s\n", query);
                    // URL-decode absolute-path
                    char* p = urldecode(abs_path);
                    if (p == NULL)
                    {
                        error(500);
                        continue;
                    }

                    // resolve absolute-path to local path
                    path = malloc(strlen(root) + strlen(p) + 1);
                    if (path == NULL)
                    {
                        error(500);
                        continue;
                    }
                    strcpy(path, root);
                    strcat(path, p);
                    free(p);

                    // ensure path exists
                    if (access(path, F_OK) == -1)
                    {
                        error(404);
                        continue;
                    }

                    // if path to directory
                    struct stat sb;
                    if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode))
                    {
                        // redirect from absolute-path to absolute-path/
                        if (abs_path[strlen(abs_path) - 1] != '/')
                        {
                            char uri[strlen(abs_path) + 1 + 1];
                            strcpy(uri, abs_path);
                            strcat(uri, "/");
                            redirect(uri);
                            continue;
                        }

                        // use path/index.php or path/index.html, if present, instead of directory's path
			printf("calling indexes with path = %s\n", path);
                        char* index = indexes(path);
                        if (index != NULL)
                        {
                            free(path);
                            path = index;
                        }
			
                        // list contents of directory
                        else
                        {
			  printf("calling list with path = %s\n", path);
                          list(path);
                          continue;
                        }
			printf("index = %s\n", index);
                    }

                    // look up MIME type for file at path
		    printf("calling lookup on path = %s\n", path);
                    const char* type = lookup(path);
                    if (type == NULL)
                    {
                        error(501);
                        continue;
                    }
		    printf("type = %s\n", type);
                    // interpret PHP script at path
                    if (strcasecmp("text/x-php", type) == 0)
                    {
		      printf("interpret being called with path = %s and query = %s\n", path, query);
                      interpret(path, query);
                    }

                    // transfer file at path
                    else
                    {
		      printf("transfer being called with path = %s and query = %s\n", path, type);
                      transfer(path, type);
                    }
                }
            }
        }
    }
}

/**
 * Checks (without blocking) whether a client has connected to server. 
 * Returns true iff so.
 */
bool connected(void)
{
  printf("calling accept(): extracts the first connection request on the queue of pending connections for the listening socket (server), sockfd, creates  a  new connected  socket, and returns a new file descriptor referring to that socket (client)\n");
    struct sockaddr_in cli_addr;
    memset(&cli_addr, 0, sizeof(cli_addr));
    socklen_t cli_len = sizeof(cli_addr);
    cfd = accept(sfd, (struct sockaddr*) &cli_addr, &cli_len);
    
    if (cfd == -1)
    {
        return false;
    }
    printf("client: addr = %ld\n", cli_addr.sin_addr.s_addr);
    return true;
}

/**
 * Responds to client with specified status code.
 */
void error(unsigned short code)
{
    // determine code's reason-phrase
    const char* phrase = reason(code);
    if (phrase == NULL)
    {
        return;
    }

    // template for response's content
    char* template = "<html><head><title>%i %s</title></head><body><h1>%i %s</h1></body></html>";

    // render template
    char body[(strlen(template) - 2 - ((int) log10(code) + 1) - 2 + strlen(phrase)) * 2 + 1];
    int length = sprintf(body, template, code, phrase, code, phrase);
    if (length < 0)
    {
        body[0] = '\0';
        length = 0;
    }

    // respond with error
    char* headers = "Content-Type: text/html\r\n";
    respond(code, headers, body, length);
}

/**
 * Frees memory allocated by scandir.
 */
void freedir(struct dirent** namelist, int n)
{
    if (namelist != NULL)
    {
        for (int i = 0; i < n; i++)
        {
            free(namelist[i]);
        }
        free(namelist);
    }
}
 
/**
 * Handles signals.
 */
void handler(int signal)
{
    // control-c
    if (signal == SIGINT)
    {
        signaled = true;
    }
}

/**
 * Escapes string for HTML. Returns dynamically allocated memory for escaped
 * string that must be deallocated by caller.
 */
char* htmlspecialchars(const char* s)
{
    // ensure s is not NULL
    if (s == NULL)
    {
        return NULL;
    }

    // allocate enough space for an unescaped copy of s
    char* t = malloc(strlen(s) + 1);
    if (t == NULL)
    {
        return NULL;
    }
    t[0] = '\0';

    // iterate over characters in s, escaping as needed
    for (int i = 0, old = strlen(s), new = old; i < old; i++)
    {
        // escape &
        if (s[i] == '&')
        {
            const char* entity = "&amp;";
            new += strlen(entity);
            t = realloc(t, new);
            if (t == NULL)
            {
                return NULL;
            }
            strcat(t, entity);
        }

        // escape "
        else if (s[i] == '"')
        {
            const char* entity = "&quot;";
            new += strlen(entity);
            t = realloc(t, new);
            if (t == NULL)
            {
                return NULL;
            }
            strcat(t, entity);
        }

        // escape '
        else if (s[i] == '\'')
        {
            const char* entity = "&#039;";
            new += strlen(entity);
            t = realloc(t, new);
            if (t == NULL)
            {
                return NULL;
            }
            strcat(t, entity);
        }

        // escape <
        else if (s[i] == '<')
        {
            const char* entity = "&lt;";
            new += strlen(entity);
            t = realloc(t, new);
            if (t == NULL)
            {
                return NULL;
            }
            strcat(t, entity);
        }

        // escape >
        else if (s[i] == '>')
        {
            const char* entity = "&gt;";
            new += strlen(entity);
            t = realloc(t, new);
            if (t == NULL)
            {
                return NULL;
            }
            strcat(t, entity);
        }

        // don't escape
        else
        {
            strncat(t, s + i, 1);
        }
    }

    // escaped string
    return t;
}

/**
 * Checks, in order, whether index.php or index.html exists inside of path.
 * Returns path to first match if so, else NULL.
 *
 * Complete the implementation of indexes in such a way that the function, given a 
 * /path/to/a/directory, returns /path/to/a/directory/index.php if index.php actually 
 * exists therein, or /path/to/a/directory/index.html if index.html actually exists 
 * therein, or NULL. In the first of those cases, this function should dynamically 
 * allocate memory on the heap for the returned string.
 *  
 */
char* indexes(const char* path)
{
  char *returnPath = NULL;
  int pathLen = 0;
  int len = 0;
  

    // TODO
  pathLen = strlen(path);
  if (strstr(path, "index.php"))
    {
      len = pathLen + 9 + 2; //add space for "/index.php" plus null
      returnPath = (char *) calloc(1, len);
      strncpy(returnPath, path, pathLen); //copy initial path to returnPath 
      strcat(returnPath, "/index.php"); 
    }
  else if (strstr(path, "index.html"))
    {
      len = pathLen + 10 + 2; //add space for "/index.html" plus null
      returnPath = (char *) calloc(1, len);
      strncpy(returnPath, path, pathLen); //copy initial path to returnPath 
      strcat(returnPath, "/index.html"); 
    }

  printf("indexes: returnPath = %s\n", returnPath);	
  return returnPath;
}

/**
 * Interprets PHP file at path using query string.
 */
void interpret(const char* path, const char* query)
{
    // ensure path is readable
    if (access(path, R_OK) == -1)
    {
        error(403);
        return;
    }

    // open pipe to PHP interpreter
    char* format = "QUERY_STRING=\"%s\" REDIRECT_STATUS=200 SCRIPT_FILENAME=\"%s\" php-cgi";
    char command[strlen(format) + (strlen(path) - 2) + (strlen(query) - 2) + 1];
    if (sprintf(command, format, query, path) < 0)
    {
        error(500);
        return;
    }
    FILE* file = popen(command, "r");
    if (file == NULL)
    {
        error(500);
        return;
    }

    // load interpreter's content
    char* content;
    size_t length;
    if (load(file, &content, &length) == false)
    {
        error(500);
        return;
    }

    // close pipe
    pclose(file);

    // subtract php-cgi's headers from content's length to get body's length
    char* haystack = content;
    printf("haystack = content returned from load() of file = %s\n", haystack);
    char* needle = strstr(haystack, "\r\n\r\n");
    if (needle == NULL)
    {
        free(content);
        error(500);
        return;
    }
    printf("needle found in haystack = %s\n", needle);
    // extract headers
    char headers[needle + 2 - haystack + 1];
    strncpy(headers, content, needle + 2 - haystack);
    headers[needle + 2 - haystack] = '\0';
    printf("headers = %s\n", headers);
    // respond with interpreter's content
    printf("calling respond--respond with interpreter's content\n");
    respond(200, headers, needle + 4, length - (needle - haystack + 4));

    // free interpreter's content
    free(content);
}

/**
 * Responds to client with directory listing of path.
 */
void list(const char* path)
{
    // ensure path is readable and executable
    if (access(path, R_OK | X_OK) == -1)
    {
        error(403);
        return;
    }

    // open directory
    DIR* dir = opendir(path);
    if (dir == NULL)
    {
        return;
    }

    // buffer for list items
    char* list = malloc(1);
    list[0] = '\0';

    // iterate over directory entries
    struct dirent** namelist = NULL;
    int n = scandir(path, &namelist, NULL, alphasort);
    for (int i = 0; i < n; i++)
    {
        // omit . from list
        if (strcmp(namelist[i]->d_name, ".") == 0)
        {
            continue;
        }

        // escape entry's name
        char* name = htmlspecialchars(namelist[i]->d_name);
        if (name == NULL)
        {
            free(list);
            freedir(namelist, n);
            error(500);
            return;
        }

        // append list item to buffer
        char* template = "<li><a href=\"%s\">%s</a></li>";
        list = realloc(list, strlen(list) + strlen(template) - 2 + strlen(name) - 2 + strlen(name) + 1);
        if (list == NULL)
        {
            free(name);
            freedir(namelist, n);
            error(500);
            return;
        }
        if (sprintf(list + strlen(list), template, name, name) < 0)
        {
            free(name);
            freedir(namelist, n);
            free(list);
            error(500);
            return;
        }

        // free escaped name
        free(name);
    }

    // free memory allocated by scandir
    freedir(namelist, n);

    // prepare response
    const char* relative = path + strlen(root);
    char* template = "<html><head><title>%s</title></head><body><h1>%s</h1><ul>%s</ul></body></html>";
    char body[strlen(template) - 2 + strlen(relative) - 2 + strlen(relative) - 2 + strlen(list) + 1];
    int length = sprintf(body, template, relative, relative, list);
    if (length < 0)
    {
        free(list);
        closedir(dir);
        error(500);
        return;
    }

    // free buffer
    free(list);

    // close directory
    closedir(dir);

    // respond with list
    char* headers = "Content-Type: text/html\r\n";
    respond(200, headers, body, length);
}

/**
 * Loads a file into memory dynamically allocated on heap.
 * Stores address thereof in *content and length thereof in *length.
 *
 * Complete the implementation of load in such a way that the function:
 *
 * --reads all available bytes from file,
 *
 * --stores those bytes contiguously in dynamically allocated memory on the heap,
 *
 * --stores the address of the first of those bytes in *content, and
 *
 * --stores the number of bytes in *length.
 *
 * Note that content is a "pointer to a pointer" (i.e., BYTE**), which means that 
 * you can effectively "return" a BYTE* to whichever function calls load by dereferencing 
 * content and storing the address of a BYTE at *content. Meanwhile, length is a pointer 
 * (i.e., size_t*), which you can also dereference in order to "return" a size_t to whichever 
 * function calls load by dereferencing length and storing a number at *length.
 */
bool load(FILE* file, BYTE** content, size_t* length)
{
  long filesize = 0;
  long numBuffers = 0; 
  long numBytesToRead = 0;
  bool returnValue = false;
  long numBytesRead = 0;
  long numBytesLastBuf = 0;
  long saveNumBufs = 0;
  BYTE *contentPtr = NULL;

  // TODO
  *length = 0;
    if (file != NULL)
    {
      printf("load a file into memory\n");
      fseek (file , 0 , SEEK_END);
      filesize = ftell (file);
      rewind (file);
      printf("file size is %ld\n", filesize);
      if (filesize > READMEGABYTEFROMFILE)
	{
	  printf("file size is greater than READMEGABYTEFROMFILE = %d\n", READMEGABYTEFROMFILE);
	  numBuffers = filesize / READMEGABYTEFROMFILE;
	  printf("numBuffers = filesize/READMEGABYTEFROMFILE = %d\n", numBuffers);
	  numBytesToRead = READMEGABYTEFROMFILE;
	  printf("numBytesToRead = READMEGABYTEFROMFILE = %d\n", numBytesToRead);
	  printf("filesize%READMEGABYTEFROMFILE = %d\n", filesize%READMEGABYTEFROMFILE);
	  if(filesize%READMEGABYTEFROMFILE > 0)
	    {
	      printf("for the left over modulo part of READMEGABYTEFROMFILE in filesize, add another buffer\n");
	      numBuffers++;
	      numBytesLastBuf = filesize%READMEGABYTEFROMFILE;
	      printf("numBuffers = %d, numBytesLastBuf = %d\n", numBuffers, numBytesLastBuf);
	    }
    	}
      else 
	{
	  printf("file size less than READMEGABYTEFROMFILE = %d\n", filesize);
	  numBuffers = 1;
	  numBytesToRead = filesize;
	  printf("numBuffers = 1, numBytesToRead = filesize = %d\n", filesize);
	}
      saveNumBufs = numBuffers;
      while (numBuffers > 0)
	{
	  if ((numBuffers == 1) && (saveNumBufs > 1))
	    {
	      numBytesToRead = numBytesLastBuf;
	      printf("numBuffers = %d, saveNumBufs = %d, numBytesToRead = numBytesLastBuf = %d\n", numBuffers, saveNumBufs, numBytesToRead);
	    }
	  printf("*content = %d before calling realloc\n", *content);
	  *content = realloc(*content, numBytesToRead);
	  if (*content == NULL)
	    {
	      printf("load: error - realloc returned NULL\n");
	      returnValue = false;
	      break;
	    }
	  else
	    returnValue = true;

	  printf("calling fread() to get %d bytes\n", numBytesToRead);
	  numBytesRead = fread (*content,1, numBytesToRead,file);
	  contentPtr = *content;
	  printf("numBytesRead = %d\n", numBytesRead);
	  printf("printing content bytes \n");
	  for (int i=0; i< numBytesRead; i++, contentPtr++)
	    printf("0x%1x ", (unsigned char)*contentPtr);
          printf("\ndone printing content bytes\n");
	  if ( (numBytesRead == 0) || (numBytesRead < numBytesToRead) )
	    {
	      if (ferror(file))
		{
		  printf("load: error reading file\n");
		  returnValue = false;
		  break;
		}
	      if (feof(file))
		{
		  printf("load: end of file reached\n");
		  returnValue = true;
		}
	    }
	  *length += numBytesRead;
	  numBuffers--;
	}
    }
    else
      {
	printf("load: error - file pointer passed is NULL\n");
	returnValue = false;
      }
    
    printf("length of file loaded in bytes = %d\n", *length);
    return returnValue;
}

/**
 * Returns MIME type for supported extensions, else NULL.
 *
 * Complete the implementation of lookup in such a way that it returns:
 *
 * text/css for any file whose path ends in .css (or any capitalization thereof),
 *
 * text/html for any file whose path ends in .html (or any capitalization thereof),
 *
 * image/gif for any file whose path ends in .gif (or any capitalization thereof),
 *
 * image/x-icon for any file whose path ends in .ico (or any capitalization thereof),
 *
 * image/jpeg (not image/jpg) for any file whose path ends in .jpg (or any capitalization thereof),
 *
 * text/javascript for any file whose path ends in .js (or any capitalization thereof),
 *
 * text/x-php for any file whose path ends in .php (or any capitalization thereof), or
 *
 * image/png for any file whose path ends in .png (or any capitalization thereof), or
 *
 * NULL otherwise.
 *
 * Odds are you’ll find functions like strcasecmp, strcpy, and/or strrchr of help! 
 */
const char* lookup(const char* path)
{
  char *pathToPeriod;
  char *lastPeriodInPath;
  
    // TODO
  printf("lookup on path = %s\n", path);
  strcpy(pathCopy, path);
  pathToPeriod = pathCopy;
  pathToPeriod = strtok(pathToPeriod, ".");
  while(pathToPeriod != NULL)
    {
      pathToPeriod = strtok(NULL, ".");
      if (pathToPeriod != NULL)
	lastPeriodInPath = pathToPeriod;
    }
  printf("pathToPeriod = %s lastPeriodInPath = %s\n", pathToPeriod, lastPeriodInPath);
  if (!strcasecmp(lastPeriodInPath, "css"))
    return "text/css";
  else if (!strcasecmp(lastPeriodInPath, "html"))
    return "text/html";
  else if (!strcasecmp(lastPeriodInPath, "gif"))
    return "image/gif";
  else if (!strcasecmp(lastPeriodInPath, "ico"))
    return "image/x-icon";
  else if (!strcasecmp(lastPeriodInPath, "jpg"))
    return "image/jpeg";
  else if (!strcasecmp(lastPeriodInPath, "js"))
    return "text/javascript";
  else if (!strcasecmp(lastPeriodInPath, "php"))
    return "text/x-php";
  else if (!strcasecmp(lastPeriodInPath, "png"))
    return "image/png";

  return NULL;
}

/**
 * Parses a request-line, storing its absolute-path at abs_path 
 * and its query string at query, both of which are assumed
 * to be at least of length LimitRequestLine + 1.
 *
 * Complete the implementation of parse in such a way that the function parses 
 * (i.e., iterates over) line, extracting its absolute-path and query and storing 
 * them at abs_path and query, respectively.
 *
 * Per 3.1.1 of http://tools.ietf.org/html/rfc7230, a request-line is defined as
 * method SP request-target SP HTTP-version CRLF
 * wherein SP represents a single space ( ) and CRLF represents \r\n. None of method, 
 * request-target, and HTTP-version, meanwhile, may contain SP.
 *
 * Per 5.3 of the same RFC, request-target, meanwhile, can take several forms, the 
 * only one of which your server needs to support is
 * absolute-path [ "?" query ]
 * whereby absolute-path (which will not contain ?) must start with / and might 
 * optionally be followed by a ? followed by a query, which may not contain ".
 *
 * Ensure that request-line (which is passed into parse as line) is consistent with 
 * these rules. If it is not, respond to the browser with 400 Bad Request and return 
 * false.
 * --if method is not GET, respond to the browser with 405 Method Not Allowed and 
 * return false;
 * --if request-target does not begin with /, respond to the browser with 501 Not 
 * Implemented and return false;
 * --if request-target contains a ", respond to the browser with 400 Bad Request and 
 * return false;
 * --if HTTP-version is not HTTP/1.1, respond to the browser with 505 HTTP Version 
 * Not Supported and return false; or
 * --If all is well, store absolute-path at the address in abs_path (which was also 
 * passed into parse as an argument). You may assume that the memory to which abs_path 
 * points will be at least of length LimitRequestLine + 1.
 *
 * Store at the address in query the query substring from request-target. If that 
 * substring is absent (even if a ? is present), then query should be "", thereby 
 * consuming one byte, whereby query[0] is '\0'. You may assume that the memory to 
 * which query points will be at least of length LimitRequestLine + 1.
 *
 * For instance, if request-target is /hello.php or /hello.php?, then query should 
 * have a value of "". And if request-target is /hello.php?q=Alice, then query should 
 * have a value of q=Alice.
 * 
 */
bool parse(const char* line, char* abs_path, char* query)
{
    // TODO
  char *queryPtr = NULL;
  int countAbsPath = 0;
  int countQuery = 0;
  char *ptrMethod = NULL;
  char *ptrReqTarget = NULL;
  char *ptrDblQuoteInReqTarget = NULL;
  char *ptrHTTPversion = NULL;
  char *ptrVersion = NULL;
  char *ptrIsThereAqForQuery = NULL;
  int len;
  
  printf("parsing line = %s\n", line);
  
  //query = NULL;
  //abs_path = NULL;
  printf("parsing for the GET method\n");
  ptrMethod = strstr(line, "GET");
  if (ptrMethod == NULL) 
  {
    error(405);
    return false;
  }
  while (*ptrMethod != ' ') //point to space past GET command
    ptrMethod++;
  ptrMethod++; //should now be pointing to request-target
  ptrReqTarget = ptrMethod;
  printf("should be pointing to request-target part of line = %s start of abs_path = %c\n", ptrReqTarget, *ptrReqTarget);
  if (*ptrReqTarget != '/')
  {
    error(501);
    return false;
  }
  else
    countAbsPath++;
  ptrReqTarget++;
  while (*ptrReqTarget != ' ')
    {
      ptrReqTarget++;
      countAbsPath++;
    }
  //len = strlen(abs_path);
  strcpy(pathChars, ptrMethod);
  pathChars[countAbsPath] = NULL;
  printf("pathChars = %s countAbsPath = %d\n", pathChars, countAbsPath);
  printf("len of abs_path = %d len of pathChars = %d\n", strlen(abs_path), strlen(pathChars));
  ptrMethod = &pathChars[0];
  strcpy(abs_path, pathChars);
  printf("parse: abs_path = %s\n", abs_path);

  ptrIsThereAqForQuery = ptrReqTarget;
  if (strstr(line, "?q"))
    {
      while (*ptrIsThereAqForQuery != '?') //point to end start of query
	{
	  ptrIsThereAqForQuery++;
	}
      ptrIsThereAqForQuery += 3; //should now be pointing just past "?q=" and at start of query string
      printf("start of query string = %s\n", ptrIsThereAqForQuery);
      ptrDblQuoteInReqTarget = strchr(line, '"');
      printf("double quote in query string? (should not be one) = %s\n", ptrDblQuoteInReqTarget);
      if (ptrDblQuoteInReqTarget != NULL)
	{
	  error(400);
	  return false;
	}
      queryPtr = ptrIsThereAqForQuery; 
      while (*ptrIsThereAqForQuery != ' ') //point to end of query
	{
	  ptrIsThereAqForQuery++;
	  countQuery++;
	}
	strncpy(query, queryPtr, countQuery);
	query[countQuery + 1] = NULL;
    }
  else //no query found
    { //even if there is a ?, query = NULL
      printf("no query found\n");
      query = "";
    }
  printf("parse: query = %s\n", query);
  ptrIsThereAqForQuery++;
 
  ptrHTTPversion = ptrIsThereAqForQuery;
  printf("are we pointing at HTTP's and it's version? %s and %s\n", ptrIsThereAqForQuery, ptrHTTPversion);
  if (strncmp(ptrHTTPversion, "HTTP/1.1", 8))
    {
      error(505);
      return false;
    }
  else
    printf("HTTP/1.1 found - correct version for now\n");

  return true;  
  
}

/**
 * Returns status code's reason phrase.
 *
 * http://www.w3.org/Protocols/rfc2616/rfc2616-sec6.html#sec6
 * https://tools.ietf.org/html/rfc2324
 */
const char* reason(unsigned short code)
{
    switch (code)
    {
        case 200: return "OK";
        case 301: return "Moved Permanently";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 414: return "Request-URI Too Long";
        case 418: return "I'm a teapot";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 505: return "HTTP Version Not Supported";
        default: return NULL;
    }
}

/**
 * Redirects client to uri.
 */
void redirect(const char* uri)
{
    char* template = "Location: %s\r\n";
    char headers[strlen(template) - 2 + strlen(uri) + 1];
    if (sprintf(headers, template, uri) < 0)
    {
        error(500);
        return;
    }
    respond(301, headers, NULL, 0);
}

/**
 * Reads (without blocking) an HTTP request's headers into memory dynamically allocated on heap.
 * Stores address thereof in *message and length thereof in *length.
 */
bool request(char** message, size_t* length)
{
    // ensure socket is open
    if (cfd == -1)
    {
        return false;
    }

    // initialize message and its length
    *message = NULL;
    *length = 0;
    
    printf("request: reading message\n");
    // read message 
    while (*length < LimitRequestLine + LimitRequestFields * LimitRequestFieldSize + 4)
    {
        // read from socket
        BYTE buffer[BYTES];
        ssize_t bytes = read(cfd, buffer, BYTES);
        if (bytes < 0)
        {
            if (*message != NULL)
            {
                free(*message);
                *message = NULL;
            }
            *length = 0;
            break;
        }

        // append bytes to message 
        *message = realloc(*message, *length + bytes + 1);
        if (*message == NULL)
        {
            *length = 0;
            break;
        }
        memcpy(*message + *length, buffer, bytes);
        *length += bytes;

        // null-terminate message thus far
        *(*message + *length) = '\0';

        // search for CRLF CRLF
        int offset = (*length - bytes < 3) ? *length - bytes : 3;
        char* haystack = *message + *length - bytes - offset;
        char* needle = strstr(haystack, "\r\n\r\n");
        if (needle != NULL)
        {
            // trim to one CRLF and null-terminate
            *length = needle - *message + 2;
            *message = realloc(*message, *length + 1);
            if (*message == NULL)
            {
                break;
            }
            *(*message + *length) = '\0';

            // ensure request-line is no longer than LimitRequestLine
            haystack = *message;
            needle = strstr(haystack, "\r\n");
            if (needle == NULL || (needle - haystack + 2) > LimitRequestLine)
            {
                break;
            }

            // count fields in message
            int fields = 0;
            haystack = needle + 2;
            while (*haystack != '\0')
            {
                // look for CRLF
                needle = strstr(haystack, "\r\n");
                if (needle == NULL)
                {
                    break;
                }

                // ensure field is no longer than LimitRequestFieldSize
                if (needle - haystack + 2 > LimitRequestFieldSize)
                {
                    break;
                }

                // look beyond CRLF
                haystack = needle + 2;
            }

            // if we didn't get to end of message, we must have erred
            if (*haystack != '\0')
            {
                break;
            }

            // ensure message has no more than LimitRequestFields
            if (fields > LimitRequestFields)
            {
                break;
            }

            // valid
            return true;
        }
    }

    // invalid
    if (*message != NULL)
    {
        free(*message);
    }
    *message = NULL;
    *length = 0;
    return false;
}

/**
 * Responds to a client with status code, headers, and body of specified length.
 */
void respond(int code, const char* headers, const char* body, size_t length)
{
  unsigned long numBytesWrittenToClient = 0;
  unsigned char *bodyPtr = &body[0];

  printf("respond(): responding to client browser with status code, headers, and body of specified length.\n");
  printf("printing body bytes which in the load routine were the content bytes passed to this respond() function \n");
  for (unsigned long i=0; i < length; i++, bodyPtr++)
    printf("0x%1x ", (unsigned char)*bodyPtr);
  printf("\ndone printing content bytes\n");

    // determine Status-Line's phrase
    // http://www.w3.org/Protocols/rfc2616/rfc2616-sec6.html#sec6.1
    const char* phrase = reason(code);
    printf("code = %d phrase = reason(code) = %s\n", code, phrase);
    if (phrase == NULL)
    {
      printf("phrase is NUll-returning from respond()\n");
        return;
    }

    //dup2(sfd, stdout);
    // respond with Status-Line
    printf("responding with status line to client\n");
    if (numBytesWrittenToClient = (dprintf(cfd, "HTTP/1.1 %i %s\r\n", code, phrase) < 0))
    {
        return;
    }
    else 
      printf("num bytes written to client browser = %d\n", numBytesWrittenToClient);

    // respond with headers
    printf("calling dprintf() to print to client browser's socket file descriptor %d with headers = %s\n", cfd, headers);
    if (numBytesWrittenToClient = (dprintf(cfd, "%s", headers) < 0))
    {
      printf("return from dprintf() less than zero--returning from respond()\n");
        return;
    }
    else 
      printf("num bytes written to client browser = %d\n", numBytesWrittenToClient);


    // respond with CRLF
    printf("calling dprintf() to print to client browser's socket file descriptor with backslash-r backslash-n \n");
    if (numBytesWrittenToClient = (dprintf(cfd, "\r\n") < 0))
    {
      printf("return from dprintf() less than zero--returning from respond()\n");
        return;
    }
    else 
      printf("num bytes written to client browser = %d\n", numBytesWrittenToClient);

    // respond with body
    printf("calling write() to client browser's socket file descriptor with body = %s length = %d\n", body, length);
    if (numBytesWrittenToClient = (write(cfd, body, length) == -1))
    {
      printf("write() of body to client browser's socket file descriptior returned minus-1 failed\n");
        return;
    }
    else 
      printf("num bytes written to client browser = %d\n", numBytesWrittenToClient);

    // log response line
    if (code == 200)
    {
        // green
      printf("code = 200, green\n");
        printf("\033[32m");
    }
    else
    {
        // red
      printf("code NOT= 200, red\n");
        printf("\033[33m");
    }
    printf("HTTP/1.1 %i %s", code, phrase);
    printf("\033[39m\n");
}

/**
 * Starts server on specified port rooted at path.
 */
void start(short port, const char* path)
{
    // path to server's root
    root = realpath(path, NULL);
    if (root == NULL)
    {
        stop();
    }

    // ensure root is executable
    if (access(root, X_OK) == -1)
    {
        stop();
    }

    // announce root
    printf("\033[33m");
    printf("Using %s for server's root", root);
    printf("\033[39m\n");

    // create a socket
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1)
    {
        stop();
    }

    // allow reuse of address (to avoid "Address already in use")
    int optval = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // assign name to socket
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
    {
        printf("\033[33m");
        printf("Port %i already in use", port);
        printf("\033[39m\n");
        stop();
    }
    
    // listen for connections
    if (listen(sfd, SOMAXCONN) == -1)
    {
        stop();
    }

    // announce port in use
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(sfd, (struct sockaddr*) &addr, &addrlen) == -1)
    {
        stop();
    }
    printf("\033[33m");
    printf("Listening on port %i", ntohs(addr.sin_port));
    printf("\033[39m\n");
}

/**
 * Stop server, deallocating any resources.
 */
void stop(void)
{
    // preserve errno across this function's library calls
    int errsv = errno;

    // announce stop
    printf("\033[33m");
    printf("Stopping server\n");
    printf("\033[39m");

    // free root, which was allocated by realpath
    if (root != NULL)
    {
        free(root);
    }

    // close server socket
    if (sfd != -1)
    {
        close(sfd);
    }

    // stop server
    exit(errsv);
}

/**
 * Transfers file at path with specified type to client.
 */
void transfer(const char* path, const char* type)
{
  BYTE *contentPtr;
    // ensure path is readable
  printf("calling access() with R_OK to make sure path is readable\n");
    if (access(path, R_OK) == -1)
    {
        error(403);
        return;
    }

    printf("opening file at path and type = %s: %s\n", path, type);
    // open file
    FILE* file = fopen(path, "r");
    if (file == NULL)
    {
        error(500);
        return;
    }

    // load file's content
    printf("calling load() to load file's content\n");
    BYTE* content = NULL;
    size_t length;
    if (load(file, &content, &length) == false)
    {
        error(500);
        return;
    }
    printf("file opened's length = %d\n", length);
    printf("printing content bytes - after returning from load()\n");
    contentPtr = content;
    for (int i=0; i< length; i++, contentPtr++)
      printf("0x%1x ", (unsigned char)*contentPtr);
    printf("\ndone printing content bytes\n");

    // close file
    fclose(file);

    // prepare response
    char* template = "Content-Type: %s\r\n";
    printf("template = %s\n", template);
    char headers[strlen(template) - 2 + strlen(type) + 1];
    printf("calling sprintf() to put the template and type into the header\n");
    if (sprintf(headers, template, type) < 0)
    {
        error(500);
        return;
    }

    // respond with file's content
    printf("calling respond() to respond to client with file's content: code = 200, headers = %s, content = %s, length = %d\n", headers, content, length);
    printf("printing content bytes - just before calling respond()\n");
    contentPtr = content;
    for (int i=0; i< length; i++, contentPtr++)
      printf("0x%1x ", (unsigned char)*contentPtr);
    printf("\ndone printing content bytes\n");
    respond(200, headers, content, length);
    printf("done responding to client browser\n");
    // free file's content
    free(content);
}

/**
 * URL-decodes string, returning dynamically allocated memory for decoded string
 * that must be deallocated by caller.
 */
char* urldecode(const char* s)
{
    // check whether s is NULL
    if (s == NULL)
    {
        return NULL;
    }

    // allocate enough (zeroed) memory for an undecoded copy of s
    char* t = calloc(strlen(s) + 1, 1);
    if (t == NULL)
    {
        return NULL;
    }
    
    // iterate over characters in s, decoding percent-encoded octets, per
    // https://www.ietf.org/rfc/rfc3986.txt
    for (int i = 0, j = 0, n = strlen(s); i < n; i++, j++)
    {
        if (s[i] == '%' && i < n - 2)
        {
            char octet[3];
            octet[0] = s[i + 1];
            octet[1] = s[i + 2];
            octet[2] = '\0';
            t[j] = (char) strtol(octet, NULL, 16);
            i += 2;
        }
        else if (s[i] == '+')
        {
            t[j] = ' ';
        }
        else
        {
            t[j] = s[i];
        }
    }

    // escaped string
    return t;
}
