#define _GNU_SOURCE

#include <stdio.h> //printf(), fgets()
//#include <unistd.h> //write()
#include <stdlib.h> //malloc(), free(), exit()
#include <errno.h>
#include <string.h> //strlen(), strcpy(), strcmp()
#include <stdint.h> //integer types with specific widths
#include <ctype.h> //tolower()

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

int32_t current_cluster;

FILE *fp = NULL;

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

int32_t NextLB(uint32_t sector)
{
  //My descr: looks up in the FAT; passing in current cluster, it provides the next cluster
  /*
    Purpose: Given a logical block address, look up into the first FAT and return the logical
    block address of the block in the file. If there is no further blocks, returns -1
  */
  
  uint32_t FATAddress = (BPB_BytsPerSec * BPB_RsvdSecCnt) + (sector * 4);
  int32_t val;
  fseek(fp, FATAddress, SEEK_SET);
  fread(&val, 4, 1, fp);
  return val;
}

void info()
{
  if (fp == NULL)
  {
    printf("Error: File not open.\n");
    return;
  }

  printf("Name\t\tHex\tBase10\n");
  printf("BPB_BytsPerSec\t0x%x\t%d\n", BPB_BytsPerSec, BPB_BytsPerSec);
  printf("BPB_SecPerClus\t0x%x\t%d\n", BPB_SecPerClus, BPB_SecPerClus);
  printf("BPB_RsvdSecCnt\t0x%x\t%d\n", BPB_RsvdSecCnt, BPB_RsvdSecCnt);
  printf("BPB_NumFATS\t0x%x\t%d\n", BPB_NumFATS, BPB_NumFATS);
  printf("BPB_FATSz32\t0x%x\t%d\n", BPB_FATSz32, BPB_FATSz32);
  printf("BPB_ExtFlags\t0x%x\t%d\n", BPB_ExtFlags, BPB_ExtFlags);
  printf("BPB_RootClus\t0x%x\t%d\n", BPB_RootClus, BPB_RootClus);
  printf("BPB_FSInfo\t0x%x\t%d\n", BPB_FSInfo, BPB_FSInfo);
}

void open(char* filename)
{
  //access call to see if filename even exists
  if (fp != NULL)
  {
    printf("Error: File system image already open.\n");
    return;
  }

  fp = fopen(filename, "r"); //maybe change to write later
  if (fp == NULL)
  {
    printf("Error: File system image not found.\n");
  }

  //read BPB values
  fseek(fp, 11, SEEK_SET);
  fread(&BPB_BytsPerSec, 2, 1, fp);
  fread(&BPB_SecPerClus, 1, 1, fp);
  fread(&BPB_RsvdSecCnt, 2, 1, fp);
  fread(&BPB_NumFATS, 1, 1, fp);
  
  fseek(fp, 36, SEEK_SET);
  fread(&BPB_FATSz32, 4, 1, fp);
  fread(&BPB_ExtFlags, 2, 1, fp);
  fseek(fp, 44, SEEK_SET);
  fread(&BPB_RootClus, 4, 1, fp);
  fread(&BPB_FSInfo, 2, 1, fp);

  current_cluster = BPB_RootClus;

  int offset = ((BPB_NumFATS * BPB_FATSz32 * BPB_BytsPerSec) + (BPB_RsvdSecCnt * BPB_BytsPerSec));
  fseek(fp, offset, SEEK_SET);

  //can read all at once since its packed
  //assumes root directory has 16 or fewer files, change to do while after most of assignment is complete
  fread(&dir[0], sizeof(struct DirectoryEntry), 16, fp);
}

void close()
{
  if (fp == NULL)
  {
    printf("Error: File system not open.\n");
    return;
  }
  fclose(fp);
  fp = NULL;
}

void ls()
{
  if (fp == NULL)
  {
    printf("Error: File system not open.\n");
    return;
  }

  for (int i = 0; i < 16; i++) //use do while later
  {
    if (dir[i].DIR_Name[0] == 0xe5)
    {
      continue;
    }

    if (dir[i].DIR_Attr == 0x01 || dir[i].DIR_Attr == 0x10 || dir[i].DIR_Attr == 0x20)
    {
      char name[9];
      char ext[4];
      memset(name, 0, sizeof(name));
      memset(ext, 0, sizeof(ext));

      strncpy(name, dir[i].DIR_Name, 8);
      strncpy(ext, dir[i].DIR_Name + 8, 3);

      //trims trailing spaces
      for (int j = 7; j >= 0; j--) 
      {
        if (name[j] == ' ')
        {
          name[j] = '\0';
        }
        else
        {
          break;
        }
      }
      for (int j = 2; j >= 0; j--) 
      {
        if (ext[j] == ' ')
        {
          ext[j] = '\0';
        }
        else
        {
          break;
        }
      }

      if (ext[0] != '\0')
      {
        printf("%-8s%s\n", name, ext);
      }
      else
      {
        printf("%s\n", name);
      }
    }
  }
}

void cd(char *directory_name)
{
  if (fp == NULL)
  {
    printf("Error: File image must be opened first.\n");
    return;
  }

  if (directory_name == NULL)
  {
    printf("Error: No directory specified.\n");
    return;
  }

  if (strcmp(directory_name, "..") == 0)
  {
    printf("HAVE NOT IMPLEMENTED YET\n");
    return;
  }
  else
  {
    char expanded_name[12];
    memset(expanded_name, ' ', 11);
    expanded_name[11] = '\0';

    strncpy(expanded_name, directory_name, strlen(directory_name));

    for (int i = 0; i < 11; i++)
    {
      expanded_name[i] = toupper(expanded_name[i]);
    }

    int found = 0;
    int dir_index = -1;

    for (int i = 0; i < 16; i++)
    {
      if (strncmp(expanded_name, dir[i].DIR_Name, 11) == 0)
      {
        if (dir[i].DIR_Attr == 0x10)
        {
          found = 1;
          dir_index = i;
          break;
        }
      }
    }

    if (!found)
    {
      printf("Directory not found.\n");
      return;
    }

    current_cluster = dir[dir_index].DIR_FirstClusterLow;

    int offset = LBAToOffset(current_cluster);
    fseek(fp, offset, SEEK_SET);

    fread(&dir[0], sizeof(struct DirectoryEntry), 16, fp);
  }
}

int main(int argc, char* argv[] )
{

  char* command_string = (char*)malloc(MAX_COMMAND_SIZE); //holds user's input command
  //char error_message[30] = "An error has occured\n";

  //char* filename = "fat32.img"; //did this for now

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
      if(strlen(argument_pointer) > 0) //skip tokens that might result from consecutive delimiters
      //argument_pointer contains token obtained by strsep()
      {
        token[token_count] = strdup(argument_pointer); //duplicate token and store in token array
        token_count++;
      }
    }
    token[token_count] = NULL;

    //CHECK TO SEE IF PROBLEM ABOVE WITH MAXNUMARG - 1 LATER

////////////////////////////////////////////////////////////////////////////
    //prints the tokenized input as a debug check
    // int token_index  = 0;
    // for( token_index = 0; token_index < token_count; token_index ++ ) 
    // {
    //   printf("token[%d] = %s\n", token_index, token[token_index] );  
    // }
///////^^this just prints tokens////////////////////////////////////////////

    //for case insensitivity//
    for (int i = 0; i < token_count; i++)
    {
      for (int j = 0; token[i][j]; j++)
      {
        token[i][j] = tolower(token[i][j]);
      }
    }

    if (token_count == 0) //skip if no tokens found and prompt user again
    {
      free(head_ptr);
      continue;
    }

    if (strcmp(token[0], "exit") == 0 || strcmp(token[0], "quit") == 0)
    {
      if (token_count != 1)
      {
        //write(STDERR_FILENO, error_message, strlen(error_message));
        printf("too many args for quitting\n"); //temp msg
      }
      else
      {
        if (fp != NULL)
        {
          fclose(fp);
          fp = NULL;
        }
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

    else if (strcmp(token[0], "open") == 0)
    {
      if (token_count != 2)
      {
        printf("Error: File system image not found.\n");
      }
      else
      {
        open(token[1]);
      }
    }
    else if (strcmp(token[0], "close") == 0)
    {
      close();
    }
    else if (strcmp(token[0], "info") == 0)
    {
      if (token_count != 1)
      {
        //write(STDERR_FILENO, error_message, strlen(error_message));
        printf("too many args for info\n"); //temp msg
      }
      else
      {
        info();
      }
    }
    else if (strcmp(token[0], "ls") == 0)
    {
      if (token_count != 1)
      {
        printf("too many args for ls\n"); //temp msg
      }
      else
      {
        ls();
      }
    }
    else if (strcmp(token[0], "cd") == 0)
    {
      if (token_count != 2)
      {
        printf("Error: Invalid command. Usage: cd <directory>\n");
      }
      else
      {
        cd(token[1]);
      }
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