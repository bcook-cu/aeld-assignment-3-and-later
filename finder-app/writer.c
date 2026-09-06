#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    
    openlog(NULL, 0, LOG_USER);

    if (argc != 3)
    {
        syslog(LOG_ERR, "Usage: %s <file> <string>\n", argv[0]);    
        closelog();
        return 1;
    }

    char writefile[4096];
    char writestr[4096];

    size_t l1 = strlen(argv[1]);
    size_t l2 = strlen(argv[2]);

    strncpy(writefile, argv[1], sizeof writefile - 1);
    writefile[l1] = '\0';
    strncpy(writestr, argv[2], sizeof writestr - 1); 
    writestr[l2] = '\0';

    FILE *file = fopen(writefile, "w");
    if (file == NULL)
    {
        
        closelog();
        return 1;
    }    
    

    size_t written = fwrite(writestr, 1, l2, file);
    if (written != l2)
    {
        syslog(LOG_ERR, "Wrote %zu of %zu bytes: %s", written, l2, strerror(errno));
        closelog();
        return 1; 
    }

    if (fclose(file) == EOF)
    {
        syslog(LOG_ERR, "fclose: %s", strerror(errno));
        closelog();
        return 1;    
    }

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    closelog();
    return 0;
}
