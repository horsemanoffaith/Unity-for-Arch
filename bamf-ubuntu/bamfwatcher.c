/* Written by: Xiao-Long Chen <chenxiaolong@cxl.epac.to> */

/* This program will monitor /usr/share/applications/ for changes and rebuild
 * /usr/share/applications/bamf.index accordingly. It will wait for pacman to
 * finish first before updating */

/* I've added some stuffs so it will not need to use perl or system() anymore.
 * Nice, right? Although it is usable, it really need some rigorous testing.
 * In the original code, the memory keeps getting larger and larger
 * every time changes in /usr/share/applications is done, where as
 * this version doesn't do that. Dunno why?
 * Being a noob in c/c++, please teach me how to code neatly.
 * -enriquezmark36@gmail.com */ 

/* Compile with (gcc|clang) bamfwatcher.c -lprocps -o bamfwatcher */

#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>
#include <proc/readproc.h>
#include <sys/inotify.h>

/* Allow the buffer to hold 64 events */
#define BUF_LEN (64 * (sizeof(struct inotify_event) + NAME_MAX + 1))
/* Directory which the bamfdaemon will watch and expect
 * its index file will be */
#define APP_DIR "/usr/share/applications/"
/* Name of the BAMF( bada** motherf***er?) index file.
 * In Ubuntu, it's bamf-2.index;
 * in Debian, it's bamf.index. ...   maybe
 * I'm not really sure, it's in the source code */
//#define BAMF_INDEX_NAME "bamf.index"
#define BAMF_INDEX_NAME "bamf-2.index"

static void print_event(struct inotify_event *event) {
  if (event->mask & IN_ACCESS)
    printf("%s: File was accessed\n",                           event->name);
  if (event->mask & IN_ATTRIB)
    printf("%s: Metadata was changed\n",                        event->name);
  if (event->mask & IN_CLOSE_WRITE)
    printf("%s: File opened for writing was closed\n",          event->name);
  if (event->mask & IN_CLOSE_NOWRITE)
    printf("%s: File not opened for writing was closed\n",      event->name);
  if (event->mask & IN_CREATE)
    printf("%s: File/directory was created\n",                  event->name);
  if (event->mask & IN_DELETE)
    printf("%s: File/directory was deleted\n",                  event->name);
  if (event->mask & IN_DELETE_SELF)
    printf("%s: Watched file/directory was deleted\n",          event->name);
  if (event->mask & IN_MODIFY)
    printf("%s: File was modified\n",                           event->name);
  if (event->mask & IN_MOVE_SELF)
    printf("%s: Watched file/directory was moved\n",            event->name);
  if (event->mask & IN_MOVED_FROM)
    printf("%s: File was moved out of the watched directory\n", event->name);
  if (event->mask & IN_MOVED_TO)
    printf("%s: File was moved into the watched directory\n",   event->name);
  if (event->mask & IN_OPEN)
    printf("%s: File was opened\n",                             event->name);
}

/* Skim the directory for entries with .desktop extension */
static inline char *desktop_file_name(DIR* directory){
   if (directory){
      struct dirent *entry;
      while ((entry = readdir(directory)) != NULL){
        /* skip hidden files */
        if (*entry->d_name == '.'){
            continue;
        }
         if (strstr(entry->d_name, ".desktop")){
            return entry->d_name;
        } 
      }
   }
   return NULL;
}

/* Return the first line of a matching string in a FILE Pointer */
static inline char *fgrep(FILE *_f, char * _s){
  static char buf[4096] = {0}; 
  if (_f != NULL){
    rewind(_f);
    while(fgets(buf, 4096, _f) != NULL){
      if(strstr(buf, _s) != NULL){
        return buf;
      }
    }
  }
  return NULL;
}

/* Extract the value of a bash-style variable
 * connected to fgrep()*/
static inline char *extract_value(FILE *_f, char *_var){
  char *ptr = NULL;
  if (_f != NULL &&
     ((ptr = fgrep(_f, _var)) != NULL) &&
     ((ptr = strchr(ptr, '=')) != NULL)      ){
        ptr[strcspn(ptr, "\n")] = '\0';
        return (ptr + 1);
  }
  return NULL;
}

void wait_for_pacman() {
  /* This is really ugly code, but I can't think of a better way to do this
   * other than a busy wait. From what I can find, kqueue is the best way to
   * solve the problem, but it doesn't exist on Linux.*/
    
  PROCTAB* proc = openproc(PROC_FILLSTAT | PROC_FILLCOM);
  proc_t processes = {0};
  
  while (readproc(proc, &processes) != NULL) {
    if (strcmp(processes.cmd , "pacman") == 0){
      printf("pacman is running. (PID %i) Waiting for it to finish...",
              processes.tid);
      fflush(stdout);
      
      while (kill(processes.tid, 0) != -1 && errno != ESRCH) {
          sleep(1);
      }
      
      printf(" Done\n");
      break;
    }
  }
  
  closeproc(proc);
}

/* Rebuilds the bamf index file in the watched directory, 
 * then returns the number of .desktop files processed */
int bamf_build() {
  /* I raise you this, an even uglier code under 80 char width. I haven't read 
   * the ebook about programming for dummies so yeah. 
   * Please deal with it. */
   
  FILE * index_file = fopen(APP_DIR BAMF_INDEX_NAME, "w");
  FILE * desktop_file = NULL;
  DIR *directory = opendir(APP_DIR);
  char full_path[PATH_MAX + 1]; 
  char *file_name = NULL; 
  char *buf = NULL; 
  char index_line[4096];
  unsigned int file_count= 0;

  if (index_file == NULL){
      fprintf(stderr, "opening the file for writing failed!\n");
      exit(1);
  }
  
  while((file_name = desktop_file_name(directory)) != NULL){
    (*index_line) = '\0';
    (*full_path) = '\0';
    sprintf(full_path, APP_DIR"%s", file_name);
    
    if ((desktop_file = fopen(full_path, "r")) != NULL){
      /* Traditions must be kept */
      printf("Reading %s...", full_path);
      fflush(stdout);
      /* The format of the file seems to be delimited by '\t'
       * and so even some of the fields aren't present 
       * there should be atleast a '\t'. Is this right? 
       * Adding a '\t' is as simple as adding this line:
       * strcat(index_line, "\t");
       * */
      
      /* desktop file name */
      strcat(index_line, file_name);
      strcat(index_line, "\t");
      
      /* Exec */
      if ((buf = extract_value(desktop_file, "Exec=")) != NULL){
        strcat(index_line, buf);
      }
      strcat(index_line, "\t");
      
      /* StartupWMClass */
      if ((buf = extract_value(desktop_file, "StartupWMClass=")) != NULL){
        strcat(index_line, buf);
      }
      strcat(index_line, "\t");
      
      /* OnlyShowIn */
      if ((buf = extract_value(desktop_file, "OnlyShowIn=")) != NULL){
        strcat(index_line, buf);
      }
      strcat(index_line, "\t");
      
      /* NoDisplay
       * based on the code, this defaults to false even it is not specified */
      /* This is the last, this must not have the '\t' */
      if ((buf = extract_value(desktop_file, "NoDisplay=")) != NULL){
        strcat(index_line, buf);
      } else {
        strcat(index_line, "false");
      }
      
      /* End this line with a newline*/
      fprintf (index_file, "%s\n", index_line);
      fclose(desktop_file);
      file_count ++;

      printf(" Done\n");      
    } else {
        fprintf(stderr, "%s:%s\n", full_path, strerror(errno));
    }
    
  }

  fclose(index_file);
  closedir(directory);
  return file_count;
}

void rebuild() {
  int count = 0;
  wait_for_pacman();
  printf("Rebuilding bamf.index...\n");
  fflush(stdout);
  count  = bamf_build();
  printf("Successfully proccessed %u files\n", count);
}

int main(int argc, char *argv[]) {
  if (getuid() != 0) {
    fprintf(stderr, "Must be run as root!\n");
    exit(1);
  }

  int fd = inotify_init();
  if (fd < 0) {
    fprintf(stderr, "inotify_init() failed!\n");
    exit(1);
  }

  if (access(APP_DIR BAMF_INDEX_NAME , F_OK) != 0) {
    rebuild();
  }

  /* Watch /usr/share/applications/ */
  int wd = inotify_add_watch(fd, APP_DIR, IN_ATTRIB |
                                                            IN_CREATE |
                                                            IN_DELETE |
                                                            IN_MOVED_TO |
                                                            IN_MOVED_FROM |
                                                            IN_MODIFY);

  if (wd < 0) {
    fprintf(stderr, "inotify_add_watch() failed!\n");
    exit(1);
  }

  char buf[BUF_LEN];

  while (1) {
    int length = read(fd, buf, BUF_LEN);
    if (length <= 0) {
      fprintf(stderr, "read() from inotify file descriptor returned %i!\n", length);
      exit(1);
    }
    //printf("Read %ld bytes from inotify file descriptor\n", (long)length);

    int i = 0;
    while (i < length) {
      struct inotify_event *event = (struct inotify_event *)&buf[i];
      /* Make sure we rebuild the index when a .desktop file is modified */
      /* Especially when someone/thing removed the index file */
      if (strstr(event->name, ".desktop") || ((event->mask & IN_DELETE) && 
          strcmp(event->name, BAMF_INDEX_NAME) == 0)){
        print_event(event);
        rebuild();
        break;
      }
      i += sizeof(struct inotify_event) + event->len;
    }
  }
}
