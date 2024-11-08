#define _GNU_SOURCE

#include <stdio.h> //printf(), fgets()
#include <unistd.h> //write()
#include <stdlib.h> //malloc(), free(), exit()
#include <errno.h>
#include <string.h> //strlen(), strcpy(), strcmp()
#include <stdint.h> //integer types with specific widths

#define WHITESPACE " \t\n" //defines delimiters when splitting command line
#define MAX_COMMAND_SIZE 255
#define MAX_NUM_ARGUMENTS 12
#define MAX_PATH 4096

char BS_OEMNName [8];
int16_t BPB_BytsPerSec; 
int8_t BPB_SecPerClus;
int16_t BPB_RsvdSecCnt;
int8_t BPB_NumFATS;
int16_t BPB_RootEntCnt;
char BS_VolLab[11];
int32_t BPB_FATSz32;
int16_t BPB_ExtFlags; //??? same here
int32_t BPB_RootClus;
int16_t BPB_FSInfo; //??? why was this not included in slides

int32_t RootDirSectors = 0;
int32_t FirstDataSector = 0;
int32_t FirstSectorofCluster = 0;

struct __attribute__((__packed__)) DirectoryEntry
{
  char DIR_Name[11];
  uint8_t DIR_Attr;
  uint8_t Unused1[8];
  uint16_t DIR_FirstClusterHigh;
  uint8_t Unused2[4];
  uint16_t DIR_FirstClusterLow;
  uint32_t DIR_FileSize;
};

struct DirectoryEntry dir[16];

int LBAToOffset(int32_t sector)
{
  //My descr: used to figure out which address to fseek to given a cluster number
  //Purpose: Finds the starting address of a block of data given the sector number
  return ((sector - 2) * BPB_BytsPerSec) + (BPB_BytsPerSec * BPB_RsvdSecCnt) + (BPB_NumFATS * BPB_FATSz32 * BPB_BytsPerSec);
}

// int16_t NextLB(uint32_t sector)
// {
//   //My descr: looks up in the FAT; passing in current cluster, it provides the next cluster
//   /*
//     Purpose: Given a logical block address, look up into the first FAT and return the logical
//     block address of the block in the file. If there is no further blocks, returns -1
//   */
  
//   uint32_t FATAddress = (BPB_BytsPerSec * BPB_RsvdSecCnt) + (sector * 4);
//   int16_t val;
//   fseek(fp, FATAddress, SEEK_SET); //does this mean that i need the file while shell is running?
//   fread(&val, 2, 1, fp); //used fd previously but I dont know where else id open it if not here
//   return val;
// }

void tempfunc() //i think i should open before starting the inf loop and close once the loop exits?
{
  FILE *fd = fopen("fat32.img", "r");
  //error handling
  fseek(fd, 11, SEEK_SET);
  fread(&BPB_BytsPerSec, 2, 1, fd);
  fread(&BPB_SecPerClus, 1, 1, fd);
  fread(&BPB_RsvdSecCnt, 2, 1, fd);
  fread(&BPB_NumFATS, 1, 1, fd);
  fseek(fd, 36, SEEK_SET);
  fread(&BPB_FATSz32, 4, 1, fd);
  fread(&BPB_ExtFlags, 2, 1, fd);
  fseek(fd, 44, SEEK_SET);
  fread(&BPB_RootClus, 4, 1, fd);
  fread(&BPB_FSInfo, 2, 1, fd);
  fclose(fd);
}

void info()
{
  //uint16_t BPB_BytsPerSec;
  //FILE *fd = fopen("fat32.img", "r");
  //error handling
  //fseek(fd, 11, SEEK_SET);
  //fread(&BPB_BytsPerSec, 2, 1, fd);
  printf("Name\t\tHex\tBase10\n");
  printf("BPB_BytsPerSec\t0x%x\t%d\n", BPB_BytsPerSec, BPB_BytsPerSec);
  printf("BPB_SecPerClus\t0x%x\t%d\n", BPB_SecPerClus, BPB_SecPerClus);
  printf("BPB_RsvdSecCnt\t0x%x\t%d\n", BPB_RsvdSecCnt, BPB_RsvdSecCnt);
  printf("BPB_NumFATS\t0x%x\t%d\n", BPB_NumFATS, BPB_NumFATS);
  printf("BPB_FATSz32\t0x%x\t%d\n", BPB_FATSz32, BPB_FATSz32);
  printf("BPB_ExtFlags\t0x%x\t%d\n", BPB_ExtFlags, BPB_ExtFlags);
  printf("BPB_RootClus\t0x%x\t%d\n", BPB_RootClus, BPB_RootClus);
  printf("BPB_FSInfo\t0x%x\t%d\n", BPB_FSInfo, BPB_FSInfo);
  //fclose(fd);
}

int main(int argc, char* argv[] )
{

  char* command_string = (char*)malloc(MAX_COMMAND_SIZE); //holds user's input command
  char error_message[30] = "An error has occured\n";

  tempfunc();

  while(1)
  {
    printf ("mfs> ");

    //reads the command from the command line
    //the while command will wait here until the user inputs something
    if (!fgets(command_string, MAX_COMMAND_SIZE, stdin))
    {
      break; //reached EOF
    }

    command_string[strcspn(command_string, "\n")] = '\0'; //replaces newline with null terminator

    if (strlen(command_string) == 0) //skip empty input lines and prompt user again
    {
      continue;
    }

    ///* Parse input *///
    char *token[MAX_NUM_ARGUMENTS]; //holds commands and arguments (tokens)

    int token_count = 0;                                 

    char *argument_pointer; //pointer to point to token parsed by strsep                                  
                                                           
    char *working_string = strdup(command_string); //duplicates string to modify copy        

    //we are going to move the working_string pointer to
    //keep track of its original value so we can deallocate
    //the correct amount at the end
    
    char *head_ptr = working_string; //used to free working_string at later point
    
    //strsep() splits working_string into tokens based on delimiters
    //each call to strsep() updates working_string to point to the next part of the string
    while (((argument_pointer = strsep(&working_string, WHITESPACE)) != NULL) && //while more tokens
              (token_count < MAX_NUM_ARGUMENTS - 1)) //reserving space for the NULL terminator
    {
      //token[token_count] = strndup( argument_pointer, MAX_COMMAND_SIZE );
      //if( strlen( token[token_count] ) == 0 )
      if(strlen(argument_pointer) > 0) //skip tokens that might result from consecutive delimiters
      //argument_pointer contains token obtained by strsep()
      {
        token[token_count] = strdup(argument_pointer); //duplicate token and store in token array
        //token[token_count] = NULL;
        token_count++;
      }
        //token_count++;
    }
    token[token_count] = NULL; //has to be NULL terminated for execvp to work

////////////////////////////////////////////////////////////////////////////
    //prints the tokenized input as a debug check
    int token_index  = 0;
    for( token_index = 0; token_index < token_count; token_index ++ ) 
    {
      printf("token[%d] = %s\n", token_index, token[token_index] );  
    }
///////^^this just prints tokens////////////////////////////////////////////

    if (token_count == 0) //skip if no tokens found and prompt user again
    {
      free(head_ptr);
      continue;
    }

    if (strcmp(token[0], "exit") == 0 || strcmp(token[0], "quit") == 0)
    {
      if (token_count != 1)
      {
        write(STDERR_FILENO, error_message, strlen(error_message));
      }
      else
      {
        free(head_ptr);
        free(command_string);
        exit(0);
      }
      for (int i = 0; i < token_count; i++)
      {
        free(token[i]);
      }
      continue;
    }

    if (strcmp(token[0], "info") == 0)
    {
      if (token_count != 1)
      {
        write(STDERR_FILENO, error_message, strlen(error_message));
      }
      else
      {
        info();
      }
      for (int i = 0; i < token_count; i++)
      {
        free(token[i]);
      }
      continue;
    }
    
    free(head_ptr); //frees memory allocated by strdup() for working_string
    for (int i = 0; i < token_count; i++) //frees each token duplicated by strdup()
    {
      free(token[i]);
    }

  }
  free(command_string);
  return 0;
}