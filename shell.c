/* This is the only file you should update and submit. */

/* Fill in your Name and GNumber in the following two comment fields
 * Name: Soltan Rahali  
 * GNumber: G01076355
 */
#include "logging.h"
#include "shell.h"

typedef struct job_struct {
  int jobId;     
  int bg;
  pid_t pid;        
  char state[20];
  char cmd[MAXLINE];
  struct job_struct *next; 
} Job;

/* Constants */
static const char *shell_path[] = {"./", "/usr/bin/", NULL};
static const char *built_ins[] = {"quit", "help", "kill", "jobs", "fg", "bg", NULL};

/*maintaining job record via linked list*/
Job *head=NULL;
/* Functions Prototypes */
void evaluate(char *cmdline, char *argv[], Cmd_aux *aux, Job **head);
void checkFiles(Cmd_aux *);
void addJob(Job** head, pid_t pid,char *cmd, int bg);
int builtin_cmd(char *argv[], Job **head);
int jobCount( Job* head);
int getJobId();
void printList(Job* head);
Job* getJob(int jobId, int pid);
void jobSwitch(char* is_bg, int jobId );
void sendSignals(int signal,pid_t pid);
void sigtstp_handler(int sig);
void sigchld_handler(int sig);
void sigint_handler(int sig);
Job *removeJob(int pid);
Job *getFgJob();


/* main */
/* The entry of your shell program */
int main() 
{
  struct sigaction ctrlc, ctrlz,act;		/* sigaction structures */
  memset(&ctrlc,0,sizeof(struct sigaction));
  memset(&ctrlz,0,sizeof(struct sigaction));
  memset(&act,0,sizeof(struct sigaction));
  ctrlc.sa_handler = sigint_handler;
  ctrlz.sa_handler = sigtstp_handler;
  act.sa_handler = sigchld_handler;
  sigaction(SIGINT, &ctrlc, NULL);	
  sigaction(SIGTSTP, &ctrlz, NULL);	
  sigaction(SIGCHLD, &act, NULL);	
  char cmdline[MAXLINE]; /* Command line */
  /* Intial Prompt and Welcome */
  log_prompt();
  log_help();
  /* Shell looping here to accept user command and execute */
  while (1) {
  
    char *argv[MAXARGS]; /* Argument list */
    Cmd_aux aux; /* Auxilliary cmd info: check shell.h */
  	/* Print prompt */
  	log_prompt();
	  /* Read a line and remove the ending '\n' */
	  if (fgets(cmdline, MAXLINE, stdin)==NULL){
	   	if (errno == EINTR)
			continue;
	    	exit(-1); 
	  }
	  if (feof(stdin)) {
	    	exit(0);
	  }
    if(!strcmp(cmdline,"\n")){
      continue;
    }
  	cmdline[strlen(cmdline)-1] = '\0';  /* remove trailing '\n' */
  	parse(cmdline, argv, &aux);		/* Parse command line */
  	/* Evaluate command */  
    if(!builtin_cmd(argv,&head)){
       evaluate(cmdline,argv,&aux,&head);
    }
  }
}

/* end main */

/* required function as your staring point; 
 * check shell.h for details
 */
void parse(char *cmdline, char *argv[], Cmd_aux *aux){

   char cmd[MAXLINE],*tokens[MAXARGS];
   strcpy(cmd,cmdline);
   char *token = strtok(cmd, " ");  
   int n=0,i=0,j=0;
   while( token != NULL ) {
      tokens[n++]=token;
      token = strtok(NULL, " ");  
   }
   /*Setting up aux members*/
   aux->is_append=-1;
   aux->is_bg=0;
   for(i=0;i<n;i++){
     if(!strcmp(tokens[i],"<")){
       aux->in_file=tokens[++i];
     }
     else if(!strcmp(tokens[i],">")){
       aux->out_file=tokens[++i];
       aux->is_append=0;
     }
     else if(!strcmp(tokens[i],">>")){
       aux->out_file=tokens[++i];
       aux->is_append=1;
     }
     else if(!strcmp(tokens[i],"&")){
       aux->is_bg=1;
     }else{
       argv[j++]=tokens[i];
     }
   }
   argv[j]=NULL; 

   
}

/*Evaluating builtin commands*/
int builtin_cmd(char *argv[], Job **head){
  
  int i=0,is_builtin=0,signal,id=0;
  pid_t pid;
  while(built_ins[i]!=NULL){
     if(!strcmp(argv[0],built_ins[i++])){
       is_builtin=1;
     }
  }
  if(!is_builtin){
     return 0;
  }
  if(!strcmp(argv[0],"help")){
    log_help();
  }else if(!strcmp(argv[0],"quit")){
      log_quit();
      exit(0);
  }else if(!strcmp(argv[0],"jobs")){
      int count = jobCount(*head);
      log_job_number(count);
      printList(*head);
  }else if(!strcmp(argv[0], "kill")){
      sscanf(argv[1],"%d",&signal);
      sscanf(argv[2],"%d",&pid);
      sendSignals(signal,pid);
  }else if(!strcmp(argv[0], "fg")||!strcmp(argv[0], "bg")){
      sscanf(argv[1],"%d",&id);
      jobSwitch(argv[0],id); 
  }

  return 1;
}
void evaluate(char *cmdline,char *argv[], Cmd_aux *aux, Job **head){

  pid_t pid;
  int i=0,status;
  char path[100],cmd[100];
  strcpy(cmd,argv[0]);
  if((pid=fork())==0){ 
    setpgid(0, 0);
    while(shell_path[i]!=NULL){
      checkFiles(aux);
      strcpy(path,shell_path[i++]);
      strcat(path,cmd);
      if((execv(path,argv))<0 && shell_path[i]==NULL){
       log_command_error(cmdline);
        exit(0);
      }
    }
  }
  /*add the job to a linked list*/ 
  addJob(&*head,pid,cmdline,aux->is_bg);
  if(!aux->is_bg){
  log_start_fg(pid,cmdline);   
    if((waitpid(pid,&status,0))>=0){ /*waiting for fg to finish*/
      if(removeJob(pid)){ /*job removed from the list*/
        log_job_fg_term(pid,cmdline); 
      }
    } 
  }else{
    log_start_bg(pid, cmdline); 
  } 
}

void sendSignals(int signal,pid_t pid){
 
   log_kill(signal,pid);
   if(kill(-pid, signal)<0){
     return;
   }
   Job* job =getJob(0,pid);
   /*no such job with the given pid*/
   if(job==NULL){
     return;
   }
   if(signal==18){
   strcpy(job->state,"Running");
   log_job_bg_cont(job->pid,job->cmd);
   }
   else if(signal==9 || signal==2){
     log_job_bg_term_sig(job->pid,job->cmd);
     removeJob(job->pid);
   }else if(signal==19){
      strcpy(job->state,"Stopped");
      log_job_bg_stopped(job->pid,job->cmd);
   }
 
   return;

}

/*Switches jobs from fg to bg and viceversa*/
void jobSwitch(char *is_bg, int jobId ){

  Job *job;
  /* if no job found with the given id*/
  if((job=getJob(jobId,0))==NULL){
    log_jobid_error(jobId);
    return;
  }
  pid_t pid=job->pid;
  char cmd[100];
  strcpy(cmd, job->cmd); 
  if(!strcmp(is_bg,"fg")){ /*moving to fg*/  
    log_job_fg(pid,job->cmd);/*log fg received*/
    job->bg=0; // becomes foreground
    if(!strcmp(job->state,"Stopped")){
     kill(-pid,SIGCONT); // resume the execution
     log_job_fg_cont(job->pid,job->cmd);
     while(getJob(0,pid)){ 
       sleep(1);  
     } 
     return;
    }else{
    int status; 
    waitpid(pid,&status,WUNTRACED);/*waiting for the fg job to finish*/
      if(WIFEXITED(status)){
          log_job_fg_term(pid,cmd); 
      }
    }    
  }else{ /*switching to bg*/ 
    log_job_bg(pid, job->cmd); /*log bg received*/
    if(!job->jobId){
      job->jobId=getJobId();
    }
    if(!strcmp(job->state,"Stopped")){
     job->bg=1;
     strcpy(job->state,"Running");
     kill(-pid,SIGCONT);
    }
  }
//  char str[10]="";
//  scanf("%s",str);
  return;
}

/*creates and add a new job to the linked list*/
void addJob(Job** head, pid_t pid,char *cmd,int bg)  {
   
    /*allocating memory for a new struct*/
    Job *newJob = (Job *)malloc(sizeof(Job));
    /*initializing struct members*/ 
    newJob->pid = pid;  
    strcpy(newJob->cmd, cmd);
    strcpy(newJob->state, "Running");
    if(bg){
      newJob->bg=1;
      newJob->jobId=getJobId();
    }else{
      newJob->bg=0;
      newJob->jobId=0;
    }
    newJob->next=NULL;  
    /*Appending the job to the list*/
    if (*head== NULL)  {  
       newJob->next = *head;
       *head= newJob;  
       return;  
    }
    Job* temp = *head;
    while (temp->next != NULL){
       temp = temp->next;
    }
    newJob->next = temp->next;
    temp->next = newJob; 
    
} 

/*returns a job struct given its jobId or pid*/
Job* getJob(int jobId, int pid){
  
  Job *current = head; 
  while (current != NULL) {
      if (current->jobId == jobId || (current->pid==pid && pid>0) ) {
        return current;
      } 
      current=current->next;
  }
  return NULL;
}

/* returns the number of jobs in the list */
int jobCount( Job* head)
{ 
    int count = 0;  
    Job* current = head;  
    while (current != NULL) { 
        count++; 
        current = current->next; 
    } 
    return count; 
}

/*returns a new background job ID*/
int getJobId(){

  int id = 0;  
  Job* current = head;  
    while (current != NULL) { 
      if(current->jobId){
        id=current->jobId; 
      } 
        current = current->next; 
    } 
    return ++id; 
}

/*Checks for input and output files */ 
void checkFiles(Cmd_aux *aux){
  
 int fd,fd1;
  if (aux->in_file) { 
    if((fd = open(aux->in_file, O_RDONLY))<0){
    exit(0);
    }
    dup2(fd, STDIN_FILENO);
    close(fd); 
  }
  if (aux->out_file) { 
    if(aux->is_append){
      fd1 = open(aux->out_file, O_WRONLY | O_APPEND | O_CREAT);
    }else{
      fd1 = open(aux->out_file, O_WRONLY | O_TRUNC | O_CREAT);
    }
    dup2(fd1, STDOUT_FILENO);
    close(fd1); 
  }
  
 
}

/*prints the list of jobs in ascending order based on jobId*/
void printList(Job* head){

   Job* temp = head;  
    while (temp != NULL) { 
       log_job_details(temp->jobId, temp->pid, temp->state, temp->cmd);
       temp = temp->next; 
    } 
}

/* Deletes a job from the list*/
Job *removeJob(int pid){
  
  Job* temp = head, *prev=NULL; 
  if (temp != NULL && temp->pid==pid) { 
    head = temp->next;      
    return temp;
  } 
  while (temp != NULL && temp->pid!=pid){ 
    prev = temp; 
    temp = temp->next; 
  } 
  if (temp == NULL) { 
    return NULL; 
  }
  prev->next = temp->next; 
  
  return temp; 
}

/*Returns a foreground job in the list if it exists*/
Job* getFgJob(){
  
  Job *current = head; 
  while (current != NULL) {
      if (!current->bg) {
        return current;
      } 
      current=current->next;
  }
  return NULL;
}

/*signal handler for SIGINT CTRl-C*/
void sigint_handler(int sig) {	
 
 log_ctrl_c();
 Job *job=getFgJob();
  if(job && job->pid) {
     kill(-job->pid,SIGINT);
     if(removeJob(job->pid)){
       log_job_fg_term_sig(job->pid,job->cmd);
     }
  }
}

/*signal handler for SIGTSTP CTRL-Z*/
void sigtstp_handler(int sig) {		
  
  log_ctrl_z();
  Job *job=getFgJob();
  if(job && job->pid) {
     kill(-job->pid,SIGTSTP);
     log_job_fg_stopped(job->pid,job->cmd);
     strcpy(job->state, "Stopped");
     job->bg=1;
  }
  
}

/*signal handler for SIGCHLD*/
void sigchld_handler(int sig) {	
 
	pid_t pid;
	int status;
 
	while ((pid = waitpid(-1, &status, WNOHANG|WUNTRACED|WCONTINUED)) > 0) 	{
    Job *job=getJob(0,pid); 
  
      if(WIFCONTINUED(status)){
        if(job->bg){
         log_job_bg_cont(job->pid,job->cmd); 
         }
      }
  		else if (WIFEXITED(status))	{ 
       if(job->bg){ 
         log_job_bg_term(job->pid,job->cmd);
       }else {
        log_job_fg_term(job->pid,job->cmd); 
       }
        removeJob(job->pid);      
		  }else if (WIFSTOPPED(status))	{ 
      
       if(job && !job->jobId){
  	    job->jobId=getJobId();
      }      
    } 
  }	
}

