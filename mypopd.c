#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024

typedef enum state {
    Undefined,
    // TODO: Add additional states as necessary
    AUTHORIZATION,
    TRANSACTION,
    UPDATE,
} State;

typedef struct serverstate {
    int fd;
    net_buffer_t nb;
    char recvbuf[MAX_LINE_LENGTH + 1];
    char *words[MAX_LINE_LENGTH];
    int nwords;
    State state;
    struct utsname my_uname;
    // TODO: Add additional fields as necessary
    mail_list_t mail;
    char* username;
} serverstate;

static void handle_client(void *new_fd);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
        return 1;
    }
    run_server(argv[1], handle_client);
    return 0;
}

// syntax_error returns
//   -1 if the server should exit
//    1 otherwise
int syntax_error(serverstate *ss) {
    if (send_formatted(ss->fd, "-ERR %s\r\n", "Syntax error in parameters or arguments") <= 0) return -1;
    return 1;
}

// checkstate returns
//   -1 if the server should exit
//    0 if the server is in the appropriate state
//    1 if the server is not in the appropriate state
int checkstate(serverstate *ss, State s) {
    if (ss->state != s) {
        if (send_formatted(ss->fd, "-ERR %s\r\n", "Bad sequence of commands") <= 0) return -1;
        return 1;
    }
    return 0;
}

// All the functions that implement a single command return
//   -1 if the server should exit
//    0 if the command was successful
//    1 if the command was unsuccessful

int do_quit(serverstate *ss) {
    // Note: This method has been filled in intentionally!
    dlog("Executing quit\n");
    send_formatted(ss->fd, "+OK Service closing transmission channel\r\n");
    ss->state = Undefined;
    return -1;
}

int do_user(serverstate *ss) {
    dlog("Executing user\n");

    if (ss->nwords < 2) {
        syntax_error(ss);
    }
    ss->username = strdup(ss->words[1]);
    int is_user = is_valid_user(ss->username, NULL);
    if (is_user != 0) {
        send_formatted(ss->fd, "+OK User %s is valid, proceed with password\r\n", ss->words[1]);
        return 0;
    } else {
        send_formatted(ss->fd, "-ERR Invalid user %s\r\n", ss->username);
        return 1;
    }
    
}

int do_pass(serverstate *ss) {
    dlog("Executing pass\n");

    if (ss->username == NULL) {
        send_formatted(ss->fd, "-ERR Enter username first\r\n");
        return 1;
    }
    
    if (ss->nwords < 2) {
        syntax_error(ss);
    }

    int is_valid_pass = is_valid_user(ss->username, ss->words[1]);
    if (is_valid_pass != 0) {
        send_formatted(ss->fd, "+OK Password is valid, mail loaded\r\n");
        ss->state = TRANSACTION;
        ss->mail = load_user_mail(ss->username);
        return 0;
    } else {
        send_formatted(ss->fd, "-ERR Invalid password for user %s\r\n", ss->username);
        return 1;
    }
}

int do_stat(serverstate *ss) {
    dlog("Executing stat\n");
    // TODO: Implement this function

    if (ss->nwords > 1) {
        syntax_error(ss);
    }

    int num_messages = mail_list_length(ss->mail, 0);
    size_t maildrop_size = mail_list_size(ss->mail);
    send_formatted(ss->fd, "+OK %d %zu\r\n", num_messages, maildrop_size);

    return 0;
}

int do_list(serverstate *ss) {
    dlog("Executing list\n");
    
    
    // invalid usage
    if (ss->nwords > 2) {
        syntax_error(ss);
        return 1;
    }
    
    
    // get number of non deleted messages
    int num_messages = mail_list_length(ss->mail, 0);
    // get number of total messages including deleted
    int total_messages = mail_list_length(ss->mail, 1);

    // if no argument is given, list all messages
    if (ss->nwords == 1) {
        
        size_t maildrop_size = mail_list_size(ss->mail); // Total size for all non-deleted messages in list

        send_formatted(ss->fd, "+OK %d messages (%zu octets)\r\n", num_messages, maildrop_size); // send to client 
        // loop through all messages, including deleted ones
        for(int i = 0; i < total_messages; i++) {
            mail_item_t mail_item = mail_list_retrieve(ss->mail, i);
            if(mail_item == NULL) {
                // if mail_item is NULL, then it is deleted, so skip it
                continue;
            } else {
                // otherwise, get size of mail_item and send to client
                size_t mail_item_sz = mail_item_size(mail_item);
                send_formatted(ss->fd, "%d %zu\r\n", i + 1, mail_item_sz);
            }
            
        }
        send_formatted(ss->fd, ".\r\n");
    // if argument is given, list only that message
    } else {
        int msg_num = atoi(ss->words[1]);
        if (msg_num > total_messages || msg_num < 1) {
            syntax_error(ss);
            return 1;
        }
        mail_item_t mail_item = mail_list_retrieve(ss->mail, msg_num - 1);
        if(mail_item == NULL) {
            send_formatted(ss->fd, "-ERR no such message\r\n");
            return 1;
        }
        size_t mail_item_sz = mail_item_size(mail_item);
        send_formatted(ss->fd, "+OK %d %zu\r\n", msg_num, mail_item_sz);
    }
    return 0;
}

int do_retr(serverstate *ss) {
    dlog("Executing retr\n");
    // TODO: implement invalid argument handling
    if(ss->nwords != 2) {
        syntax_error(ss);
        return 1;
    }
    int msg_num = atoi(ss->words[1]);

    if(msg_num < 1) {
        syntax_error(ss);
        return 1;
    }

    
    mail_item_t mail_item = mail_list_retrieve(ss->mail, msg_num - 1);
    
    FILE *file = mail_item_contents(mail_item);
    
    if(file==NULL) {
        send_formatted(ss->fd, "-ERR no such message\r\n");
        return 1;
    }
    
    char buffer[MAX_LINE_LENGTH];
    size_t bytes_read;
    send_formatted(ss->fd, "+OK Message follows\r\n");
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send_all(ss->fd, buffer, bytes_read) < 0) {
            // Handle error when sending
            fclose(file);
            send_formatted(ss->fd, "-ERR Error sending message\r\n");
            return 1;
        }
    }

    fclose(file);
    send_formatted(ss->fd, ".\r\n");
    return 0;
}

int do_rset(serverstate *ss) {
    dlog("Executing rset\n");
    // TODO: Implement this function
    if (ss->nwords > 1) {
        syntax_error(ss);
        return 1;
    }
    int undeleted = mail_list_undelete(ss->mail);
    send_formatted(ss->fd, "+OK %d message(s) restored\r\n", undeleted);
    return 0;
}

int do_noop(serverstate *ss) {
    dlog("Executing noop\n");
    send_formatted(ss->fd, "+OK\r\n");
    return 0;
}

int do_dele(serverstate *ss) {
    dlog("Executing dele\n");

    if (ss->nwords != 2) {
        syntax_error(ss);
        return 1;
    }
    int msg_num = atoi(ss->words[1]);
    int total_num_mail = mail_list_length(ss->mail, 1);

    if (msg_num < 1 || msg_num > total_num_mail) {
        syntax_error(ss);
        return 1;
    }
    mail_item_t mail_item = mail_list_retrieve(ss->mail, msg_num - 1);
    //check if mail is deleted
    if (mail_item == NULL) {
        send_formatted(ss->fd, "-ERR Message %d already deleted\r\n", msg_num);
        return 1;
    }
    mail_item_delete(mail_item);
    // send_formatted(ss->fd, mail_list_retrieve(mail, msg_num - 1) == NULL ? "Message %d deleted\n" : "Message %d not deleted\n", msg_num);
    send_formatted(ss->fd, "+OK Message %d deleted\r\n", msg_num);
    return 0;
}

void handle_client(void *new_fd) {
    int fd = *(int *)(new_fd);

    size_t len;
    serverstate mstate, *ss = &mstate;

    ss->fd = fd;
    ss->nb = nb_create(fd, MAX_LINE_LENGTH);
    ss->state = Undefined;
    uname(&ss->my_uname);
    // TODO: Initialize additional fields in `serverstate`, if any
    if (send_formatted(fd, "+OK POP3 Server on %s ready\r\n", ss->my_uname.nodename) <= 0) return;
    ss->state = AUTHORIZATION;

    while ((len = nb_read_line(ss->nb, ss->recvbuf)) >= 0) {
        if (ss->recvbuf[len - 1] != '\n') {
            // command line is too long, stop immediately
            send_formatted(fd, "-ERR Syntax error, command unrecognized\r\n");
            break;
        }
        if (strlen(ss->recvbuf) < len) {
            // received null byte somewhere in the string, stop immediately.
            send_formatted(fd, "-ERR Syntax error, command unrecognized\r\n");
            break;
        }
        // Remove CR, LF and other space characters from end of buffer
        while (isspace(ss->recvbuf[len - 1])) ss->recvbuf[--len] = 0;

        dlog("%x: Command is %s\n", fd, ss->recvbuf);
        if (strlen(ss->recvbuf) == 0) {
            send_formatted(fd, "-ERR Syntax error, blank command unrecognized\r\n");
            break;
        }
        // Split the command into its component "words"
        ss->nwords = split(ss->recvbuf, ss->words);
        char *command = ss->words[0];

        /* TODO: Handle the different values of `command` and dispatch it to the correct implementation
         *  TOP, UIDL, APOP commands do not need to be implemented and therefore may return an error response */
         // handle QUIT command
        
        if (strcasecmp(command, "QUIT") == 0) {
            // if server state is anything other than AUTHORIZATION, TRANSACTION, or UPDATE, then do nothing
            // otherwise execute QUIT command
            do_quit(ss);
            break;
            
        } else if (strcasecmp(command, "NOOP") == 0) {
            int state = checkstate(ss, TRANSACTION);
            if (state == -1) {
                do_quit(ss);
                break;
            } else if (state == 0) {
                do_noop(ss);
            }
        } else if (strcasecmp(command, "USER") == 0) {
            int state = checkstate(ss, AUTHORIZATION);
            if (state == -1) {
                do_quit(ss);
                break;
            } else if (state == 0) {
                do_user(ss);
            }
        } else if (strcasecmp(command, "PASS") == 0) {
            int state = checkstate(ss, AUTHORIZATION);
            if (state == -1) {
                do_quit(ss);
                break;
            } else if (state == 0) {
                do_pass(ss);
            }
        } else if (strcasecmp(command, "STAT") == 0) {
            int state = checkstate(ss, TRANSACTION);
            if (state == -1) {
                do_quit(ss);
                break;
            } else if (state == 0) {
                do_stat(ss);
            }
        } else if (strcasecmp(command, "LIST") == 0) {
            int state = checkstate(ss, TRANSACTION);
            if (state == -1) {
                do_quit(ss);
                break;
            } else if (state == 0) {
                do_list(ss);
            }
        } else if (strcasecmp(command, "RETR") == 0) {
            int state = checkstate(ss, TRANSACTION);
            if (state == -1) {
                do_quit(ss);
                break;
            } else if (state == 0) {
                do_retr(ss);
            }
        } else if (strcasecmp(command, "RSET") == 0) {
            int state = checkstate(ss, TRANSACTION);
            if (state == -1) {
                do_quit(ss);
                break;
            } else if (state == 0) {
                do_rset(ss);
            }
        } else if (strcasecmp(command, "DELE") == 0) {
            int state = checkstate(ss, TRANSACTION);
            if (state == -1) {
                do_quit(ss);
                break;
            } else if (state == 0) {
                do_dele(ss);
            }
        } else {
            syntax_error(ss);
        }
        
    }
    // TODO: Clean up fields in `serverstate`, if required
    mail_list_destroy(ss->mail);
    free(ss->username);
    nb_destroy(ss->nb);
    close(fd);
    free(new_fd);
}
