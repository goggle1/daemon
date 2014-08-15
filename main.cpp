
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

typedef char 		SInt8;
typedef u_int16_t  	Bool16;

static pid_t sChildPID = 0;

Bool16 RunInForeground()
{

    #if __linux__ || __MacOSX__
         (void) setvbuf(stdout, NULL, _IOLBF, 0);
         //OSThread::WrapSleep(true);
    #endif
    
    return true;
}


Bool16 sendtochild(int sig, pid_t myPID)
{
    if (sChildPID != 0 && sChildPID != myPID) // this is the parent
    {   // Send signal to child
        ::kill(sChildPID, sig);
        return true;
    }

    return false;
}


void sigcatcher(int sig, int /*sinfo*/, struct sigcontext* /*sctxt*/)
{

    fprintf(stdout, "Signal %d caught\n", sig);

    pid_t myPID = getpid();
    //
    // SIGHUP means we should reread our preferences
    if (sig == SIGHUP)
    {
        if (sendtochild(sig,myPID))
        {
            return;
        }
        else
        {
            // This is the child process.
            // Re-read our preferences.
            #if 0
            RereadPrefsTask* task = new RereadPrefsTask;
            task->Signal(Task::kStartEvent);
			#endif

        }
    }
        
    //Try to shut down gracefully the first time, shutdown forcefully the next time
    if (sig == SIGINT) // kill the child only
    {
        if (sendtochild(sig,myPID))
        {
            return;// ok we're done 
        }
        else
        {
			//
			// Tell the server that there has been a SigInt, the main thread will start
			// the shutdown process because of this. The parent and child processes will quit.
			#if 0
			if (sSigIntCount == 0)
				QTSServerInterface::GetServer()->SetSigInt();
			sSigIntCount++;
			#endif
		}
    }
	
	if (sig == SIGTERM || sig == SIGQUIT) // kill child then quit
    {
        if (sendtochild(sig,myPID))
        {
             return;// ok we're done 
        }
        else
        {
			// Tell the server that there has been a SigTerm, the main thread will start
			// the shutdown process because of this only the child will quit
    		#if 0
    
			if (sSigTermCount == 0)
				QTSServerInterface::GetServer()->SetSigTerm();
			sSigTermCount++;
			#endif
		}
    }
}

int daemon(int nochdir, int noclose)
{
    int fd;

    switch (fork()) {
    case -1:
        return (-1);
    case 0:
        break;
    default:
        _exit(0);
    }

    if (setsid() == -1)
        return (-1);

    if (!nochdir)
        (void)chdir("/");

    if (!noclose && (fd = open("/dev/null", O_RDWR, 0)) != -1) {
        (void)dup2(fd, STDIN_FILENO);
        (void)dup2(fd, STDOUT_FILENO);
        (void)dup2(fd, STDERR_FILENO);
        if (fd > 2)
            (void)close (fd);
    }
    return (0);
}



int main(int argc, char* argv[])
{
	int ret = 0;

	struct sigaction act;
	sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = (void(*)(int))&sigcatcher;
	(void)::sigaction(SIGPIPE, &act, NULL);
    (void)::sigaction(SIGHUP, &act, NULL);
    (void)::sigaction(SIGINT, &act, NULL);
    (void)::sigaction(SIGTERM, &act, NULL);
    (void)::sigaction(SIGQUIT, &act, NULL);
    (void)::sigaction(SIGALRM, &act, NULL);

	//grow our pool of file descriptors to the max!
    struct rlimit rl;    
    // set it to the absolute maximum that the operating system allows - have to be superuser to do this
    rl.rlim_cur = RLIM_INFINITY;
    rl.rlim_max = RLIM_INFINITY; 
    setrlimit (RLIMIT_NOFILE, &rl);

	int dontFork = 0;
	int status = 0;
    int pid = 0;
    pid_t processID = 0;

    if (!dontFork)
    {
        if (daemon(0, 0) != 0)
        {
            fprintf(stderr, "Failed to daemonize process. Error = %d [%s]\n", errno, strerror(errno));
            exit(-1);
        }
    }
	
    if ( !dontFork) // if (fork) 
    {
        //loop until the server exits normally. If the server doesn't exit
        //normally, then restart it.
        // normal exit means the following
        // the child quit 
        do // fork at least once but stop on the status conditions returned by wait or if autoStart pref is false
        {
            processID = fork();            
            if (processID > 0) // this is the parent and we have a child
            {
                sChildPID = processID;
                status = 0;
                while (status == 0) //loop on wait until status is != 0;
                {	
                 	pid = ::wait(&status);
                 	SInt8 exitStatus = (SInt8) WEXITSTATUS(status);
                	//qtss_printf("Child Process %d wait exited with pid=%d status=%d exit status=%d\n", processID, pid, status, exitStatus);
                	
					if (WIFEXITED(status) && pid > 0 && status != 0) // child exited with status -2 restart or -1 don't restart 
					{
						//qtss_printf("child exited with status=%d\n", exitStatus);
						
						if ( exitStatus == -1) // child couldn't run don't try again
						{
							fprintf(stderr, "child exited with -1 fatal error so parent is exiting too.\n");
							exit (EXIT_FAILURE); 
						}
						break; // restart the child
							
					}
					
					if (WIFSIGNALED(status)) // child exited on an unhandled signal (maybe a bus error or seg fault)
					{	
						//qtss_printf("child was signalled\n");
						break; // restart the child
					}

                 		
                	if (pid == -1 && status == 0) // parent woken up by a handled signal
                   	{
						//qtss_printf("handled signal continue waiting\n");
                   		continue;
                   	}
                   	
                 	if (pid > 0 && status == 0)
                 	{
                 		//qtss_printf("child exited cleanly so parent is exiting\n");
                 		exit(EXIT_SUCCESS);                		
                	}
                	
                	//qtss_printf("child died for unknown reasons parent is exiting\n");
                	exit (EXIT_FAILURE);
                }
            }
            else if (processID == 0) // must be the child
				break;
            else
            	exit(EXIT_FAILURE);
            	
            	
            //eek. If you auto-restart too fast, you might start the new one before the OS has
            //cleaned up from the old one, resulting in startup errors when you create the new
            //one. Waiting for a second seems to work
            sleep(1);
        } while (1); // fork again based on pref if server dies
        if (processID != 0) //the parent is quitting
        	exit(EXIT_SUCCESS);   

        
    }
    sChildPID = 0;
    //we have to do this again for the child process, because sigaction states
    //do not span multiple processes.
    (void)::sigaction(SIGPIPE, &act, NULL);
    (void)::sigaction(SIGHUP, &act, NULL);
    (void)::sigaction(SIGINT, &act, NULL);
    (void)::sigaction(SIGTERM, &act, NULL);
    (void)::sigaction(SIGQUIT, &act, NULL);

	while(1)
	{
		sleep(1);
	}
	
	
	return ret;
}



