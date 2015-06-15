#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <glob.h>

#define CommandSize 256
#define PathSize 256
#define ErrorMsgSize 300
#define MaximumPipe 2
#define MAXFILENUM 500

#define Built_in_Command 1
#define Argument 2
#define Non_Built_in_Command 3
#define Pipe 4

#define ExecuteSuccess 0
#define ExecutePathValid 0
#define ExecuteNotFound 9
#define ExecuteError 10

#define IfFileExises 0

#define CommandValid 0
#define ArgumentValid 0
#define CommandIllegalCharacter 1
#define CommandWrongNumArguments 2
#define CommandTooManyPipe 3
#define CommandExtraPipe 4

#define BeginSignal 0
#define ResumeSignal 1

#define true 1
#define false 0

#define pid_t int

typedef struct MyToken
{
    int type;
    int argcount;
    pid_t pid;
    int isOver;
    int isSusp;
    int status;
    char* cname;
    char** arguments;
    struct MyToken* next;
} CommandToken;

typedef struct MyList
{
    CommandToken* chead;
    pid_t gid;
    int isBGJob;
    char command[CommandSize];
    struct MyList* next;
} CommandList;


CommandList *commandlist;
char commandline[CommandSize];
char sherror[ErrorMsgSize];
pid_t shgid;
FILE* fperr;

int CheckCommand(CommandToken*);
int CheckArgument(CommandToken*);
int CheckPipe(CommandToken*);
int Checkcd(CommandToken*);
int Checkjobs(CommandToken*);
int Checkfg(CommandToken*);
int Checkexit(CommandToken*);
int CheckOther(CommandToken*);
int CheckPathConstraint(CommandToken*);
int CheckListOver(CommandList*);
int CheckListSusp(CommandList*);
void RemovePipe(CommandToken*);
void InitList(CommandList*, CommandToken*);

CommandToken* DeToken(char command[CommandSize], int*);
void FreeToken(CommandToken*);
void FreeCommandList(CommandList*);
void InitCommand(CommandToken*);
void PrepareCommand(CommandToken*);
char** ExpandCommand(char* wildcard, int* numOfFile);
char** PrepareArgument(CommandToken*);
void JudgeType(CommandToken*);
void ProcessCommand(char command[CommandSize]);
int Execute(CommandToken*);
void Executejobs();
void Executefg(CommandToken*);
void ExecuteExit();
int ExecuteCommand(CommandToken* command, int, int, pid_t, int);
int ExecuteList(CommandList*);
void WaitCommandList(CommandList*);
int StartCommandList(CommandList*, int);
void ChangeCommandStatus(pid_t, int);
void ResetCommandStatus(CommandList*);
char** ExpandCommand(char *, int *);
void FreeTempStrArray(char**, int);

void ProcessCommand(char command[CommandSize])
{
    int i;
    for (i = 0; i < (int)strlen(command); i++)
    {
        if (command[i] == '\t')
            command[i] = ' ';
        if (command[i] == '\n')
            command[i] = '\0';
    }
}

int CheckCommand(CommandToken* command)
{
    int flag = CommandValid;
    CommandToken* head = command;
    if (!strcmp(command->cname, "cd") || !strcmp(command->cname, "exit") || \
        !strcmp(command->cname, "jobs") || !strcmp(command->cname, "fg"))
    {
        if (!strcmp(command->cname, "cd"))
            flag = Checkcd(command);
        else
            if (!strcmp(command->cname, "exit"))
                flag = Checkexit(command);
            else
                if (!strcmp(command->cname, "jobs"))
                    flag = Checkjobs(command);
                else
                    flag = Checkfg(command);
        if (command->next)
            return CommandTooManyPipe;
        return flag;
    }
    else
        while (flag == CommandValid && command)
        {
            if (!strcmp(command->cname, "cd") || !strcmp(command->cname, "exit") || \
                !strcmp(command->cname, "jobs") || !strcmp(command->cname, "fg"))
                return CommandTooManyPipe;
            if (strcmp(command->cname, "|"))
                 flag = CheckOther(command);
            command = command->next;
        }
    if (flag == CommandValid)
        return CheckPipe(head);
    else
        return flag;
}

int CheckPipe(CommandToken* command)
{
    int pipe = 0;
    int comc = 0;
    while (command)
    {
        if (!strcmp(command->cname, "|"))
            ++pipe;
        else
            ++comc;
        command = command-> next;
    }
    if (pipe > MaximumPipe)
        return CommandTooManyPipe;
    else
        if (pipe >= comc)
            return CommandExtraPipe;
        else
            return CommandValid;
}

int CheckArgument(CommandToken* command)
{
    int i = 0, j = 0;
    for (; i < command->argcount; i++)
        for (j = 0; j < (int)strlen(command->arguments[i]); j++)
            if (command->arguments[i][j] == '|' || command->arguments[i][j] == '<' || \
                command->arguments[i][j] == '>' || command->arguments[i][j] == '!' || \
                command->arguments[i][j] == '\'' || command->arguments[i][j] == '"' || \
                command->arguments[i][j] == '`')
                return CommandIllegalCharacter;
    return ArgumentValid;
}

int Checkcd(CommandToken* command)
{
    int flag = CheckArgument(command);
    if (flag != ArgumentValid)
        return flag;
    if (command->argcount != 1)
        return CommandWrongNumArguments;
    return CommandValid;
}

int Checkexit(CommandToken* command)
{
    int flag = CheckArgument(command);
    if (flag != ArgumentValid)
        return flag;
    if (command->argcount)
        return CommandWrongNumArguments;
    return CommandValid;
}

int Checkjobs(CommandToken* command)
{
    int flag = CheckArgument(command);
    if (flag != ArgumentValid)
        return flag;
    if (command->argcount)
        return CommandWrongNumArguments;
    return CommandValid;
}

int Checkfg(CommandToken* command)
{
    int flag = CheckArgument(command);
    if (flag != ArgumentValid)
        return flag;
    if (command->argcount == 0) {
    	command->argcount = 1;
	command->arguments = (char **)malloc(sizeof(char *));
	command->arguments[0] = (char *)malloc(sizeof(char) * 2);
	command->arguments[0][0] = '1';
	command->arguments[0][1] = '\0';
    }
    if (command->argcount != 1)
        return CommandWrongNumArguments;
    return CommandValid;
}

int CheckOther(CommandToken* command)
{
    int i = 0;
    for (; i < (int)strlen(command->cname); i++)
        if (command->cname[i] == '|' || command->cname[i] == '*' || \
            command->cname[i] == '>' || command->cname[i] == '<' || \
            command->cname[i] == '!' || command->cname[i] == '"' || \
            command->cname[i] == '\'' || command->cname[i] == '`')
            return CommandIllegalCharacter;
    if (command->argcount)
        return CheckArgument(command);
    return CommandValid;
}

int CheckPathConstraint(CommandToken* command)
{
    char tmp[CommandSize] = "";
    if (!strncmp(command->cname, "../", 3) || !strncmp(command->cname, "./", 2) || command->cname[0] == '/')
    {
        realpath(command->cname, tmp);
        free(command->cname);
        command->cname = (char*)malloc(sizeof(char) * ((int)strlen(tmp) + 1));
        memset(command->cname, 0, sizeof(char) * ((int)strlen(tmp) + 1));
        strcpy(command->cname, tmp);
        return ExecutePathValid;
    }
    return ExecutePathValid;
}

CommandToken* DeToken(char command[CommandSize], int* argu_num)
{
    char tmp[CommandSize] = "";
    char *ptr;
    CommandToken* cptr = NULL, *ctmp, *head;
    int commandflag = 1;
    *argu_num = 0;

    strcpy(tmp, command);
    if ((ptr = strtok(tmp, " ")) != NULL)
    {
        do
        {
            if (1 == strlen(ptr) && ptr[0] == 10)
                break;
	    if (1 == strlen(ptr) && ptr[0] == '&')
                continue;
            if (1 == strlen(ptr) && ptr[0] == '|')
                commandflag = 1;

            if (commandflag)
            {
                ctmp = cptr;
                cptr = (CommandToken *)malloc(sizeof(CommandToken));
                if (ctmp)
                    ctmp->next = cptr;
                InitCommand(cptr);
                cptr->cname = (char *)malloc(sizeof(char) * (strlen(ptr) + 1));
                memset(cptr->cname, 0, sizeof(char) * (strlen(ptr) + 1));
                strcpy(cptr->cname, ptr);
                JudgeType(cptr);
                if (cptr->type != Pipe)
                    commandflag = 0;
            }
            else
            {
                cptr->arguments[cptr->argcount] = (char*)malloc(sizeof(char) * (strlen(ptr) + 1));
                memset(cptr->arguments[cptr->argcount], 0, sizeof(char) * (strlen(ptr) + 1));
                strcpy(cptr->arguments[cptr->argcount], ptr);
                cptr->argcount++;
            }
            ++(*argu_num);
            if ((*argu_num) == 1)
                head = cptr;
        } while ((ptr = strtok(NULL, " ")) != NULL);
    }
    else
    {
        cptr = (CommandToken *)malloc(sizeof(CommandToken));
        cptr = (CommandToken *)malloc(sizeof(CommandToken));
        InitCommand(cptr);
        cptr->cname = (char *)malloc(sizeof(char) * (strlen(ptr) + 1));
        memset(cptr->cname, 0, sizeof(char) * (strlen(ptr) + 1));
        strcpy(cptr->cname, ptr);
        JudgeType(cptr);
        *argu_num = 1;
        return cptr;
    }
    return head;
}

void InitCommand(CommandToken* command)
{
    command->type = 0;
    command->argcount = 0;
    command->arguments = (char **)malloc(sizeof(char *) * CommandSize / 2);
    memset(command->arguments, 0, sizeof(char *) * CommandSize / 2);
    command->next = NULL;
    command->pid = (pid_t)0;
    command->isOver = 0;
    command->isSusp = 0;
    command->status = -1;
}

void InitList(CommandList* newlist, CommandToken* command)
{
    CommandList* head = commandlist;
    memset(newlist->command, 0, sizeof(char) * CommandSize);
    newlist->gid = (pid_t)0;
    newlist->next = NULL;
    newlist->chead = command;
    strcpy(newlist->command, commandline);
    if (commandline[(int)strlen(commandline) - 1] == '&')
	newlist->isBGJob = 1;
    else
	newlist->isBGJob = 0;
    if (!head)
        commandlist = newlist;
    else
    {
        while (head->next)
            head = head->next;
        head->next = newlist;
    }
    PrepareCommand(newlist->chead);
}

void PrepareCommand(CommandToken* command)
{
    CommandToken *tmp, *head = command;
    while (head && head->next)
    {
        if (head->next->type == Pipe)
        {
            tmp = head->next;
            head->next = head->next->next;
            FreeToken(tmp);
        }
        head = head->next;
    }
}

char** ExpandCommand(char* wildcard, int* numOfFile)
{
    glob_t globbuf;
    
    glob(wildcard, GLOB_NOCHECK, NULL, &globbuf);
    *numOfFile = globbuf.gl_pathc;
    return globbuf.gl_pathv; 
}

char** PrepareArgument(CommandToken* command)
{
    char **arg, **tmp, **tmp_exp;
    int i = 1, j, numOfFile, numOfFile_tmp, total = 0;
    
    tmp = (char **) malloc (sizeof(char*) * MAXFILENUM);
    for (j = 0; j < command->argcount; j++)
        if (strchr(command->arguments[j], '*') != NULL)
        {
            numOfFile = 0;
            tmp_exp = ExpandCommand(command->arguments[j], &numOfFile);
            if (tmp_exp)
            {
                numOfFile_tmp = numOfFile;
                while (numOfFile)
                {
                    tmp[total] = (char*)malloc(sizeof(char) * ((int)strlen(tmp_exp[numOfFile_tmp-numOfFile]) + 1));
                    memset(tmp[total], 0, sizeof(char) * ((int)strlen(tmp_exp[numOfFile_tmp-numOfFile]) + 1));
                    strcpy(tmp[total], tmp_exp[numOfFile_tmp-numOfFile]);
                    total++;
                    numOfFile--;
                }
                FreeTempStrArray(tmp_exp, numOfFile_tmp);
            }
            else
            {
                tmp[total] = (char*)malloc(sizeof(char) * ((int)strlen(command->arguments[j]) + 1));
                memset(tmp[total], 0, sizeof(char) * ((int)strlen(command->arguments[j]) + 1));
                strcpy(tmp[total], command->arguments[j]);
                total++;
            }
        }
        else
        {
            tmp[total] = (char*)malloc(sizeof(char) * ((int)strlen(command->arguments[j]) + 1));
            memset(tmp[total], 0, sizeof(char) * ((int)strlen(command->arguments[j]) + 1));
            strcpy(tmp[total], command->arguments[j]);
            total++;
        }
        
    arg = (char**)malloc(sizeof(char*) * (total + 2));
    arg[total+1] = NULL;
    arg[0] = (char*)malloc(sizeof(char) * ((int)strlen(command->cname) + 1));
    memset(arg[0], 0, sizeof(char) * ((int)strlen(command->cname) + 1));
    strcpy(arg[0], command->cname);
    for (; i <= total; i++)
    {
        arg[i] = (char*)malloc(sizeof(char) * ((int)strlen(tmp[i-1]) + 1));
        memset(arg[i], 0, sizeof(char) * ((int)strlen(tmp[i-1]) + 1));
        strcpy(arg[i], tmp[i-1]);
    }
    FreeTempStrArray(tmp, total);
    return arg;
}

void FreeToken(CommandToken* command)
{
    int i = 0;
    for (; i < command->argcount; i++)
        free(command->arguments[i]);
    free(command->arguments);
    free(command->cname);
    free(command);
}

void FreeTempStrArray(char** tmp, int num)
{
    int i;
    for (i = 0; i < num; i++)
        free(tmp[i]);
    free(tmp);
}

void FreeCommandList(CommandList* list)
{
    CommandToken *head = list->chead, *tmp = list->chead;
    CommandList* clist = commandlist;
    if (commandlist == list)
        commandlist = list->next;
    else
    {
        while (list != clist->next)
            clist = clist->next;
        clist->next = clist->next->next;
    }

    while (head)
    {
        tmp = head->next;
        FreeToken(head);
        head = tmp;
    }
    free(list);
}

void JudgeType(CommandToken* command)
{
    if (!strcmp(command->cname, "cd") || !strcmp(command->cname, "exit") \
            || !strcmp(command->cname, "jobs") || !strcmp(command->cname, "fg"))
        command->type = Built_in_Command;
    else
        if (!strcmp(command->cname, "|"))
            command->type = Pipe;
        else
            command->type = Non_Built_in_Command;
}

int Execute(CommandToken* command)
{
    CommandToken* head = command;
    CommandList* newlist = NULL;
    if (!strcmp(command->cname, "cd"))
    {
        if (chdir(command->arguments[0]) == -1)
        {
            strcpy(sherror, command->arguments[0]);
            strcat(sherror, ": cannot change directory");
            return ExecuteError;
        }
        else
            return ExecuteSuccess;
    }
    else
        if (!strcmp(command->cname, "jobs"))
        {
            Executejobs();
            return ExecuteSuccess;
        }
        else
            if (!strcmp(command->cname, "fg"))
            {
                Executefg(command);
                return ExecuteSuccess;
            }
            else
                if (!strcmp(command->cname, "exit"))
                {
                    ExecuteExit();
                    return ExecuteSuccess;
                }
                else
                {
                    if (CheckPathConstraint(head) == ExecuteNotFound) // TEST
                    {
                        strcpy(sherror, command->arguments[0]);
                        strcat(sherror, ": command not found");
                        return ExecuteError;
                    }
                    newlist = (CommandList*)malloc(sizeof(CommandList));
                    InitList(newlist, command);
                    return ExecuteList(newlist);
                }
    return ExecuteSuccess;
}

void ExecuteExit()
{
    if (commandlist)
    {
        printf("There is at least one suspended job\n");
    }
    else
    {
        printf("[ Shell Terminated ]\n");
        exit(0);
    }
}

void Executejobs()
{
    CommandList* list = commandlist;
    int i = 0;
    if (list == NULL)
        printf("No suspended jobs\n");
    while (list)
    {
        i++;
        printf("[%d]  %s\n", i, list->command);
        list = list->next;
    }
}

void Executefg(CommandToken* command)
{
    int i = 0, flag = false, arg = atoi(command->arguments[0]);
    CommandList* list = commandlist;
    while (list)
    {
        i++;
        if (i == arg)
        {
            flag = true;
            break;
        }
        list = list->next;
    }
    if (flag)
    {
        ResetCommandStatus(list);
        StartCommandList(list, ResumeSignal);
    }
    else
        printf("fg:  no such job\n");
}


int ExecuteCommand(CommandToken* command, int in, int out, pid_t gid, int isBGJob)
{
    pid_t pid;
    char** arg = PrepareArgument(command);

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);


    setenv("PATH", "/bin:/usr/bin:.", 1);

    pid = getpid();
    if (setpgid(pid, gid) < 0)
    {
        printf("Error:  cannot set child process gid\n");
        exit(1);
    }
    if (!isBGJob) {
    	tcsetpgrp(STDIN_FILENO, gid);
    }
    else { 
        printf("aaa");
        tcsetpgrp(STDIN_FILENO, shgid);
    }
    if (in != STDIN_FILENO)
    {
        dup2(in, STDIN_FILENO);
        close(in);
    }
    if (out != STDOUT_FILENO)
    {
        dup2(out, STDOUT_FILENO);
        close(out);
    }
    if (command->cname[0] == '/')
        execv(command->cname, arg);
    else
        execvp(command->cname, arg);
    if (errno == ENOENT)
        exit(ExecuteNotFound);
    exit(ExecuteError);
}

int ExecuteList(CommandList* newlist)
{

    int pipefile[2];
    CommandToken* head = newlist->chead;
    int in, out;
    pid_t cpid;

    in = STDIN_FILENO;
    while (head)
    {
        if (head->next)
        {
            if (pipe(pipefile) < 0)
            {
                if ((int)strlen(sherror) == 0)
                    strcpy(sherror, "Error:  cannot create pipe");
                return ExecuteError;
            }
            else
                out = pipefile[1];
        }
        else
            out = STDOUT_FILENO;
        cpid = fork();
        if (cpid == 0)
        {
            cpid = getpid();
            if (!newlist->gid)
                newlist->gid = cpid;
            head->pid = cpid;
            ExecuteCommand(head, in, out, newlist->gid, newlist->isBGJob);
        }
        else
            if (cpid < 0)
            {
                if ((int)strlen(sherror) == 0)
                    strcpy(sherror, "Error:  cannot create child process");
                return ExecuteError;
            }
            else
            {
                if (!newlist->gid)
                    newlist->gid = cpid;
                head->pid = cpid;
                if (setpgid(cpid, newlist->gid) < 0)
                {
                    printf("Error: cannot set child process gid\n");
                    exit(1);
                }
            }
        if (in != STDIN_FILENO)
            close(in);
        if (out != STDOUT_FILENO)
            close(out);
        in = pipefile[0];
        head = head->next;
    }

    return StartCommandList(newlist, BeginSignal);
}

int StartCommandList(CommandList* list, int signal)
{
    int status, flagerror = false, isSusp = false;
    CommandToken* command = NULL, *head = NULL;

    if (signal == ResumeSignal)
        if (kill(- list->gid, SIGCONT) < 0)
            return ExecuteError;
    if (list->isBGJob && signal == BeginSignal) {
        tcsetpgrp(STDIN_FILENO, shgid);
	return ExecuteSuccess;
    }
    else { 
	tcsetpgrp(STDIN_FILENO, list->gid);
    }
    
    WaitCommandList(list);
    
    tcsetpgrp(STDIN_FILENO, shgid);
    
    head = list->chead;
    while (head)
    {
        if (head->status == ExecuteNotFound)
        {
            command = head;
            status = ExecuteNotFound;
            break;
        }
        else
            if (head->status == ExecuteError)
            {
                flagerror = true;
                command = head;
            }
            else
                if (head->isSusp)
                {
                    isSusp = true;
                    break;
                }
        head = head->next;
    }
    if (status != ExecuteNotFound)
    {
        if (flagerror)
            status = ExecuteError;
        else
            if (isSusp)
            {
                printf("\n");
                return ExecuteSuccess;
            }
            status = ExecuteSuccess;
    }

    if (status == ExecuteNotFound)
    {
        strcpy(sherror, command->cname);
        strcat(sherror, ":  command not found");
    }
    else
        if (status == ExecuteError)
        {
            strcpy(sherror, command->cname);
            strcat(sherror, ":  unknown error");
        }
        else
            if (status == ExecuteSuccess)
            {
                FreeCommandList(list);
                return ExecuteSuccess;
            }
    FreeCommandList(list);
    return ExecuteError;
}

void WaitCommandList(CommandList* list)
{
    pid_t pid;
    int status;
    do
    {
        pid = waitpid(WAIT_ANY, &status, WUNTRACED);
        if (pid < 0)
            return;
        ChangeCommandStatus(pid, status);
    }
    while (!CheckListOver(list) && !CheckListSusp(list));

}

int CheckListOver(CommandList* list)
{
    CommandToken* head = list->chead;
    while (head)
    {
        if (head->isOver) {
	    kill(-list->gid, SIGPIPE);
            return true;
	}
        head = head->next;
    }
    return false;
}

int CheckListSusp(CommandList* list)
{
    CommandToken* head = list->chead;
    while (head)
    {
        if (!head->isSusp) 
            return false;
        head = head->next;
    }
    return true;
}

void ResetCommandStatus(CommandList* list)
{
    CommandToken* head = list->chead;
    while (head)
    {
        head->isSusp = 0;
        head->isOver = 0;
        head->status = 0;
        head = head->next;
    }
}


void ChangeCommandStatus(pid_t pid, int status)
{
    CommandList* list = commandlist;
    CommandToken* head;
    while (list)
    {
        head = list->chead;
        while (head)
        {
            if (head->pid == pid)
            {
                if (WIFSTOPPED(status))
                    head->isSusp = 1;
                else
                {
                    head->isOver = 1;
                    head->status = WEXITSTATUS(status);
                }
                return;
            }
            head = head->next;
        }
        list = list->next;
    }
}

int main()
{
    int argu_num;
    char cpath[PathSize];
    char* fp = NULL;
    CommandToken* token, *head;
    int flag, i;

    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    setenv("PATH", "/bin:/usr/bin:.", 1);
 
    shgid = getpid();
    if (setpgid(shgid, shgid) < 0)
    {
        printf("Error:  cannot set shell gid\n");
        exit(1);
    }
    tcsetpgrp (STDIN_FILENO, shgid);

    for (;;)
    {
        memset(commandline, 0, sizeof(char) * CommandSize);
        memset(cpath, 0, sizeof(char) * PathSize);
        memset(sherror, 0, sizeof(char) * ErrorMsgSize);
        getcwd(cpath, PathSize - 1);
        printf("[3150 shell:%s]$ ", cpath);
        memset(sherror, 0, sizeof(sherror));
        if ((fp = fgets(commandline, CommandSize, stdin)) != NULL)
        {
            flag = false;
            for (i = 0; i < (int)strlen(commandline); i++)
                if (commandline[i] != '\n' && commandline[i] != ' ')
                {
                    flag = true;
                    break;
                }
            if (!flag)
                continue;
            ProcessCommand(commandline);
            if ((token = DeToken(commandline, &argu_num)) != NULL)
            {
                head = token;
                switch (CheckCommand(token))
                {
                    case CommandValid:
                        break;
                    case CommandIllegalCharacter:
                    case CommandTooManyPipe:
                    case CommandExtraPipe:
                        printf("Error:  invalid input command line\n");
                        continue;
                    case CommandWrongNumArguments:
                        printf("%s:  wrong number of arguments\n", head->cname);
                        continue;
                    default:
                        break;
                }

                if (Execute(token) != ExecuteSuccess)
                    printf("%s\n", sherror);
            }
            else
                printf("Error:  invalid input command line\n");
        }
        else
        {
            printf("\n");
            ExecuteExit();
        }
    }
    return 0;
}
