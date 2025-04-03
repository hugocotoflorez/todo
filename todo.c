/* todo.c
 *
 * Desc:
 * A simple to-do list for the terminal written in C.
 * You can add tasks with dates and retrieve them later
 * by time frames like a week. With http server!
 *
 * Author: Hugo Coto Florez
 * Repo: https://github.com/hugootoflorez/todo
 * License: licenseless
 * Standard: C11
 * ------------------------------------------------------*/

/* It has to be defined to use strptime and mktime
 * and greater to 499 to use strdup */
#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FLAG_IMPLEMENTATION
#include "flag.h"

#include "da.h"

#define PORT 5002
#define MAX_ATTEMPTS 10
#define MAX_CLIENTS 16

#define BACKUP_PATH "/home/hugo/"
#define TMP_FOLDER "/tmp/"
#define HIDEN "."

#define IN_FILENAME BACKUP_PATH "todo.out"
#define OUT_FILENAME BACKUP_PATH "todo.out"
#define PID_FILENAME TMP_FOLDER "todo-daemon-pid"
#define LOG_FILENAME BACKUP_PATH HIDEN "log.txt"
#define CSS_NO_FILE "none"
#define UNREACHABLE(...)                                                            \
        do {                                                                        \
                printf("Unreachable code!" __VA_OPT__(": %s") "\n", ##__VA_ARGS__); \
                exit(1);                                                            \
        } while (0)


#define TODO(what)
#define ZERO(obj_ptr) memset((obj_ptr), 0, sizeof(obj_ptr)[0])
#define strcatf(strbuf, what, ...) sprintf((strbuf) + strlen(strbuf), what, ##__VA_ARGS__)
#define TRUNCAT(str, chr)                         \
        do {                                      \
                char *_c_;                        \
                if ((_c_ = strchr((str), (chr)))) \
                        *_c_ = 0;                 \
        } while (0)

#define DATETIME_FORMAT "%c"

typedef struct {
        time_t due;
        char *name;
        char *desc;
} Task;

typedef DA(Task) Task_da;

Task_da data;

/* Chatgpt prime */
const char *no_tasks_messages[] = {
        "No tasks for this date! Enjoy your free time.",
        "You're all caught up! Maybe start something new?",
        "Nothing to do! A perfect time for a break.",
        "No tasks here! How about planning ahead?",
        "You're task-free! Go do something fun.",
        "No pending tasks! Maybe check your goals?",
        "All clear! Time to relax or explore new ideas.",
        "No deadlines today! Make the most of it.",
        "Nothing due now! Maybe organize your workspace?",
        "You're ahead of schedule! Keep up the great work."
};

bool *quiet = NULL;
char **css_file;

static char *
overload_date(time_t time)
{
        static char global_datetime_buffer[64];
        strftime(global_datetime_buffer, sizeof global_datetime_buffer - 1, DATETIME_FORMAT, localtime(&time));
        return global_datetime_buffer;
}

int
compare_tasks_by_date(const void *a, const void *b)
{
        Task *ea = (Task *) a;
        Task *eb = (Task *) b;
        return (int) (ea->due - eb->due);
}

void
add_if_valid(Task task)
{
        if (task.name && task.due) {
                da_append(&data, task);
        }
}

void
list_tasks(int fd, Task_da d, const char *format, ...)
{
        va_list arg;
        va_start(arg, format);
        qsort(d.data, d.size, sizeof *d.data, compare_tasks_by_date);

        if (!*quiet) {
                vdprintf(fd, format, arg);
                dprintf(fd, ":\n");
        }
        for_da_each(e, d)
        {
                dprintf(fd, "%d: %s (%s)", da_index(e, d), e->name, overload_date(e->due));
                dprintf(fd, e->desc ? ": %s\n" : "\n", e->desc);
        }
        if (d.size == 0 && !*quiet)
                dprintf(fd, "  %s\n", no_tasks_messages[rand() % 10]);
}

void
kill_self()
{
        pid_t pid;
        int fd;

        sem_t *sem;
        sem = sem_open("/todo_pid_file_sem", O_CREAT, 0600, 1);
        assert(sem != SEM_FAILED);

        sem_wait(sem);

        fd = open(PID_FILENAME, O_RDONLY | O_CREAT, 0600);
        assert(fd >= 0);
        while (read(fd, &pid, sizeof pid) == sizeof pid) {
                kill(pid, SIGTERM);
        }
        close(fd);

        fd = open(PID_FILENAME, O_WRONLY | O_TRUNC | O_CREAT, 0600);
        assert(fd >= 0);
        pid = getpid();
        assert(write(fd, &pid, sizeof pid) == sizeof pid);
        close(fd);

        sem_post(sem);
}

void
spawn_serve()
{
        static int port = PORT;
        struct sockaddr_in sock_in;
        socklen_t addr_len;
        int sockfd;
        int clientfd;
        char client_ip[INET_ADDRSTRLEN];
        char css_file_buf[128];
        char inbuf[128] = { 0 };
        char buf[1024 * 1024] = { 0 };
        int n;
        int fd;

        /* As fork is called twice it is not attacked to terminal */
        if (fork() != 0) {
                exit(0);
        }

        if (fork() != 0) {
                exit(0);
        }

        kill_self();

        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        assert(sockfd >= 0);

        sock_in.sin_family = AF_INET;
        sock_in.sin_addr.s_addr = htonl(INADDR_ANY);

retry:
        errno = 0;
        sock_in.sin_port = htons(port);

        if (bind(sockfd, (struct sockaddr *) &sock_in, sizeof(struct sockaddr_in)) < 0) {
                if (errno == EADDRINUSE) {
                        ++port;
                        if (port - PORT > MAX_ATTEMPTS) {
                                perror("Bind max attempts");
                                exit(1);
                        }
                        goto retry;
                }
                perror("Bind");
                exit(1);
        }

        assert(listen(sockfd, MAX_CLIENTS) >= 0);

        printf("http://127.0.0.1:%d\n", port);

        fflush(stdout);
        fflush(stderr);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        while (1) {
                int clicked_elem_index;
                addr_len = sizeof(struct sockaddr_in);

                if (((clientfd = accept(sockfd, (struct sockaddr *) &sock_in, &addr_len)) < 0)) {
                        perror("Accept");
                        exit(-1);
                }

                switch (fork()) {
                case 0:
                        switch (read(clientfd, inbuf, sizeof inbuf - 1)) {
                        case 0:
                        case -1:
                                fprintf(stderr, "Internal Server Error! Reload the page\n");
                                continue;
                        default:
                                if (sscanf(inbuf, "GET /?button=%d HTTP/1.1\r\n", &clicked_elem_index) == 1) {
                                        switch (clicked_elem_index) {
                                        default:
                                                da_remove(&data, clicked_elem_index);
                                                break;
                                        case -1:
                                                return;
                                        }
                                }
                        }

                        inet_ntop(AF_INET, &sock_in.sin_addr, client_ip, addr_len);

                        qsort(data.data, data.size, sizeof *data.data, compare_tasks_by_date);

                        strcatf(buf, "<!DOCTYPE html>");
                        strcatf(buf, "<html>");
                        strcatf(buf, "<head>");

                        if (strcmp(*css_file, CSS_NO_FILE) != 0) {
                                strcatf(buf, "<style>");
                                fd = open(*css_file, O_RDONLY);
                                assert(fd >= 0);
                                while ((n = read(fd, css_file_buf, sizeof css_file_buf - 1)) > 0) {
                                        css_file_buf[n] = 0;
                                        strcatf(buf, "%s", css_file_buf);
                                }
                                close(fd);
                                strcatf(buf, "</style>");
                        }

                        strcatf(buf, "</head>");
                        strcatf(buf, "<body>");
                        strcatf(buf, "<title>");
                        strcatf(buf, "Todo");
                        strcatf(buf, "</title>");
                        strcatf(buf, "<h1>");
                        strcatf(buf, "Tasks");
                        strcatf(buf, "</h1>");
                        strcatf(buf, "<dl>");
                        for_da_each(e, data)
                        {
                                strcatf(buf, "<dt>");
                                strcatf(buf, "%s", e->name);
                                strcatf(buf, "<form action=\"/\" method=\"GET\" style=\"display:inline;\">");
                                strcatf(buf, "<input type=\"hidden\" name=\"button\" value=\"%d\">", da_index(e, data));
                                strcatf(buf, "<button type=\"submit\">Done</button>");
                                strcatf(buf, "</form>");
                                strcatf(buf, "<dd>");
                                strcatf(buf, "%s", overload_date(e->due));
                                strcatf(buf, "</dd>");
                                if (e->desc) {
                                        strcatf(buf, "<dd><p>");
                                        strcatf(buf, "%s\n", e->desc);
                                        strcatf(buf, "</p></dd>");
                                }
                        }
                        strcatf(buf, "</dl>");
                        strcatf(buf, "<br>");
                        strcatf(buf, "<form action=\"/\" method=\"GET\" style=\"display:inline;\">");
                        strcatf(buf, "<input type=\"hidden\" name=\"button\" value=\"%d\">", -1);
                        strcatf(buf, "<button type=\"submit\">Save and quit</button>");
                        strcatf(buf, "</form>");
                        strcatf(buf, "</body>");
                        strcatf(buf, "</html>");

                        dprintf(clientfd, "HTTP/1.1 200 OK\r\n");
                        dprintf(clientfd, "Content-Type: text/html\r\n");
                        dprintf(clientfd, "Content-Length: %zu\r\n", strlen(buf));
                        dprintf(clientfd, "\r\n");

                        assert(send(clientfd, buf, strlen(buf), 0) > 0);

                        close(clientfd);
                        exit(0);
                }
        }
        close(sockfd);
}

int
load_from_file(const char *filename)
{
        FILE *f;
        char buf[128];
        Task task = { 0 };
        struct tm tp;
        char *c;

        f = fopen(filename, "r");
        if (f == NULL) {
                fprintf(stderr, "File %s can not be opened! You should create it\n", filename);
                return 0;
        }

        while (fgets(buf, sizeof buf - 1, f)) {
                switch (buf[0]) {
                        /* NAME */
                case '[':
                        add_if_valid(task);
                        ZERO(&task);
                        TRUNCAT(buf, ']');
                        task.name = strdup(buf + 1);
                        break;

                        /* DESCRIPTION */
                case ' ':
                        if (!memcmp(buf + 2, "desc: ", 6)) {
                                TRUNCAT(buf + 8, '\n');
                                task.desc = strdup(buf + 8);
                        }

                        /* DATE TIME */
                        else if (!memcmp(buf + 2, "date: ", 6)) {
                                TRUNCAT(buf, '\n');
                                ZERO(&tp);
                                if ((c = strptime(buf + 8, DATETIME_FORMAT, &tp)) && *c) {
                                        fprintf(stderr, "Can not load %s\n", buf + 8);
                                }

                                tp.tm_isdst = -1; // determine if summer time is in use (+-1h)
                                task.due = mktime(&tp);
                        }

                        /* INVALID ARGUMENT */
                        else
                                fprintf(stderr, "Unknown token: %s\n", buf);
                        break;

                case '\n':
                        break;
                default:
                        fprintf(stderr, "Unknown token: %s\n", buf);
                        break;
                }
        }

        add_if_valid(task);
        fclose(f);
        return 1;
}

int
load_to_file(const char *filename)
{
        FILE *f;
        f = fopen(filename, "w");

        if (f == NULL) {
                fprintf(stderr, "File %s can not be opened to write!\n", filename);
                return 0;
        }

        for_da_each(task, data)
        {
                fprintf(f, "[%s]\n", task->name);
                fprintf(f, "  date: %s\n", overload_date(task->due));
                if (task->desc)
                        fprintf(f, "  desc: %s\n", task->desc);
                fprintf(f, "\n");
        }

        fclose(f);
        return data.size;
}

time_t
days(unsigned int days)
{
        time_t t;
        struct tm *tp;
        t = time(NULL) + days * (3600 * 24);
        tp = localtime(&t);
        tp->tm_hour = 23;
        tp->tm_min = 59;
        tp->tm_sec = 59;
        tp->tm_isdst = -1; // determine if summer time is in use (+-1h)
        return mktime(tp);
}

time_t
next_sunday(int *d)
{
        time_t t;
        struct tm *tp;
        t = time(NULL);
        tp = localtime(&t);
        tp->tm_isdst = -1; // determine if summer time is in use (+-1h)
        mktime(tp);
        if (d)
                *d = 7 - tp->tm_wday;
        return days(7 - tp->tm_wday);
}

/* Get a subarray of DATA whose end date is before TP */
Task_da
tasks_before(struct tm tp)
{
        tp.tm_isdst = -1; // determine if summer time is in use (+-1h)
        time_t time = mktime(&tp);
        Task_da filtered_data = { 0 };

        for_da_each(task, data)
        {
                if (difftime(task->due, time) <= 0)
                        da_append(&filtered_data, *task);
        }
        return filtered_data;
}

void
destroy_all()
{
        for_da_each(e, data)
        {
                free(e->name);
                free(e->desc);
        }
        da_destroy(&data);
}

void
usage(FILE *stream)
{
        fprintf(stream, "Usage: %s [OPTIONS]\n", flag_program_name());
        fprintf(stream, "OPTIONS:\n");
        flag_print_options(stream);
}

void
add_task()
{
        Task task = { 0 };
        char buf[128];
        time_t t = time(0);
        struct tm tp_current = *localtime(&t);
        struct tm tp = { 0 };
        int n;

        /* Name */
        printf("Task name: ");
        fflush(stdout);
        if (!fgets(buf, sizeof buf - 1, stdin)[1]) {
                return;
        }
        TRUNCAT(buf, '\n');
        task.name = strdup(buf);

        /* Desc */
        printf("  Desc: ");
        fflush(stdout);
        if (fgets(buf, sizeof buf - 1, stdin)[1]) {
                TRUNCAT(buf, '\n');
                task.desc = strdup(buf);
        }

        /* Date */
        printf("  | +N: N days from today\n");
        printf("  | DD: Day DD of current month\n");
        printf("  | DD/MM: Day DD of MM month\n");
        printf("  | DD/MM/YYYY: Day DD of MM month of year YY\n");
        printf("  Date format: ");
        fflush(stdout);
        fgets(buf, sizeof buf - 1, stdin);
        if (sscanf(buf, "+%d", &n) == 1) {
                t += n * (3600 * 24);
                tp = *localtime(&t);
        } else if (sscanf(buf, "%d/%d/%d", &tp.tm_mday, &tp.tm_mon, &tp.tm_year) == 3) {
        } else if (sscanf(buf, "%d/%d", &tp.tm_mday, &tp.tm_mon) == 2) {
                tp.tm_year = tp_current.tm_year;
                --tp.tm_mon;
        } else if (sscanf(buf, "%d", &tp.tm_mday) == 1) {
                tp.tm_year = tp_current.tm_year;
                tp.tm_mon = tp_current.tm_mon;

        } else {
                fprintf(stderr, "Error: can not parse date: %s\n", buf);
                free(task.name);
                free(task.desc);
                return;
        }

        /* Time */
        printf("  | +N: N hours from now\n");
        printf("  | HH MM: hour HH and minutes MM\n");
        printf("  | Defaults to 23:59:59\n");
        printf("  Time format: ");
        fflush(stdout);
        fgets(buf, sizeof buf - 1, stdin);
        if (sscanf(buf, "+%d", &n) == 1) {
                struct tm tp2;
                t = time(NULL) + n * (3600 * 24);
                tp2 = *localtime(&t);
                tp.tm_hour = tp2.tm_hour + n;
                tp.tm_min = tp2.tm_min;
                tp.tm_sec = tp2.tm_sec;
        } else if (sscanf(buf, "%d %d", &tp.tm_hour, &tp.tm_min) == 2) {
                tp.tm_sec = 0;
        } else {
                tp.tm_hour = 23;
                tp.tm_min = 59;
                tp.tm_sec = 59;
        }

        tp.tm_isdst = -1; // determine if summer time is in use (+-1h)
        task.due = mktime(&tp);

        da_append(&data, task);
}


int
main(int argc, char *argv[])
{
        bool *help = flag_bool("help", false, "Print this help and exit");
        bool *today = flag_bool("today", false, "Show tasks due today");
        bool *week = flag_bool("week", false, "Show tasks due this week (tasks before Sunday)");
        int *in = flag_int("in", -1, "Show tasks due in the next N days");
        bool *overdue = flag_bool("overdue", false, "Show tasks that are past their due date");
        int *done = flag_int("done", -1, "Mark task N as completed");
        bool *clear = flag_bool("clear", false, "Mark all tasks as completed");
        bool *add = flag_bool("add", false, "Add a new task");
        char **in_file = flag_str("in_file", IN_FILENAME, "Input file");
        char **out_file = flag_str("out_file", IN_FILENAME, "Output file");
        css_file = flag_str("css_file", CSS_NO_FILE, "CSS file");
        bool *serve = flag_bool("serve", false, "Start http server daemon");
        bool *die = flag_bool("die", false, "Kill running daemon");
        quiet = flag_bool("quiet", false, "Do not show unneded output");

        if (!flag_parse(argc, argv)) {
                usage(stderr);
                flag_print_error(stderr);
                exit(1);
        }

        if (load_from_file(*in_file) == 0) {
                destroy_all();
                exit(0);
        }
        srand(time(0));

        if (*help) {
                usage(stdout);
                destroy_all();
                exit(0);
        }
        if (*add) {
                add_task();
        }

        if (*done >= 0) {
                qsort(data.data, data.size, sizeof *data.data, compare_tasks_by_date);
                da_remove(&data, *done);
        }

        if (*clear) {
                data.size = 0;
        }

        if (*today) {
                time_t time = days(0);
                Task_da filter = tasks_before(*localtime(&time));
                list_tasks(STDOUT_FILENO, filter, "Tasks for today");
        }

        else if (*overdue) {
                time_t t = time(NULL);
                Task_da filter = tasks_before(*localtime(&t));
                list_tasks(STDOUT_FILENO, filter, "Overdue tasks");
                da_destroy(&filter);
        }

        else if (*in >= 0) {
                time_t time = days(*in);
                Task_da filter = tasks_before(*localtime(&time));
                list_tasks(STDOUT_FILENO, filter, "Tasks for %d days", *in);
                da_destroy(&filter);
        }


        else if (*week) {
                time_t t = next_sunday(NULL);
                Task_da filter = tasks_before(*localtime(&t));
                list_tasks(STDOUT_FILENO, filter, "Tasks before Sunday");
                da_destroy(&filter);
        }

        else if (*serve) {
                spawn_serve();
        }

        else if (*die) {
                kill_self();
                sem_unlink("/todo_pid_file_sem");

        }

        else {
                list_tasks(STDOUT_FILENO, data, "Tasks");
        }

        load_to_file(*out_file);
        destroy_all();
        return 0;
}
