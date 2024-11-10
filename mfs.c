#define _GNU_SOURCE

#include <stdio.h> //printf(), fgets()
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
int16_t BPB_ExtFlags;
int32_t BPB_RootClus;
int16_t BPB_FSInfo;

int32_t RootDirSectors = 0;
int32_t FirstDataSector = 0;
int32_t FirstSectorofCluster = 0;

int32_t current_cluster;
int32_t previous_cluster[50];
int prev_cluster_count = 0;

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

void expand_filename(char *filename, char *expanded_name)
{
  memset(expanded_name, ' ', 11);
  expanded_name[11] = '\0';

  char *token = strtok(filename, ".");
  if (token != NULL)
  {
    strncpy(expanded_name, token, strlen(token));
    token = strtok(NULL, ".");
    if (token != NULL)
    {
      strncpy((char*)(expanded_name + 8), token, strlen(token));
    }
  }
  else
  {
    strncpy(expanded_name, filename, strlen(filename));
  }

  for (int i = 0; i < 11; i++)
  {
    expanded_name[i] = toupper(expanded_name[i]);
  }
}

void info()
{
  if (fp == NULL)
  {
    printf("Error: File system image must be opened first.\n");
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

  fp = fopen(filename, "r+"); //maybe change to write later
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

void ls(char *path)
{
  if (fp == NULL)
  {
    printf("Error: File system not open.\n");
    return;
  }

  int32_t target_cluster;

  if (strcmp(path, ".") == 0)
  {
    target_cluster = current_cluster;
  }
  else if(strcmp(path, "..") == 0)
  {
    if (prev_cluster_count > 0)
    {
      target_cluster = previous_cluster[prev_cluster_count - 1];
    }
    else
    {
      printf("Error: No parent directory.\n");
      return;
    }
  }

  int offset = LBAToOffset(target_cluster);
  fseek(fp, offset, SEEK_SET);

  struct DirectoryEntry temp_dir[16];
  fread(&temp_dir, sizeof(struct DirectoryEntry), 16, fp);

  for (int i = 0; i < 16; i++) //use do while later
  {
    //if char is signed, it interprets 0xE5 as -27
    if (((unsigned char)temp_dir[i].DIR_Name[0]) == 0xE5)
    {
      continue;
    }

    if (temp_dir[i].DIR_Attr == 0x01 || temp_dir[i].DIR_Attr == 0x10 || temp_dir[i].DIR_Attr == 0x20)
    {
      char name[9];
      char ext[4];
      memset(name, 0, sizeof(name));
      memset(ext, 0, sizeof(ext));

      strncpy(name, temp_dir[i].DIR_Name, 8);
      strncpy(ext, temp_dir[i].DIR_Name + 8, 3);

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

void stat(char *filename)
{
  if (fp == NULL)
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }

  char expanded_name[12];
  expand_filename(filename, expanded_name);

  int found = 0;

  for (int i = 0; i < 16; i++)
  {
    if (strncmp(expanded_name, dir[i].DIR_Name, 11) == 0)
    {
      found = 1;
      printf("Attribute\tSize\tStarting Cluster Number\n");
      printf("0x%x\t\t", dir[i].DIR_Attr);
      printf("%d\t", dir[i].DIR_FileSize);
      printf("%d\n", dir[i].DIR_FirstClusterLow);
      break;
    }
  }

  if (!found)
  {
    printf("Error: File not found\n");
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

  if (strcmp(directory_name, ".") == 0)
  {
    printf("Already in current directory.\n");
    return;
  }
  else if(strcmp(directory_name, "..") == 0)
  {
    if (prev_cluster_count > 0)
    {
      current_cluster = previous_cluster[prev_cluster_count - 1];
      prev_cluster_count--;

      int offset = LBAToOffset(current_cluster);
      fseek(fp, offset, SEEK_SET);

      fread(&dir[0], sizeof(struct DirectoryEntry), 16, fp);
    }
    else
    {
      printf("Error: No parent directory.\n");
    }
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
    //please don't have more than 50 subdirectories :)
    previous_cluster[prev_cluster_count] = current_cluster;
    prev_cluster_count++;
    current_cluster = dir[dir_index].DIR_FirstClusterLow;

    int offset = LBAToOffset(current_cluster);
    fseek(fp, offset, SEEK_SET);

    fread(&dir[0], sizeof(struct DirectoryEntry), 16, fp);
  }
}

void read(char *filename, int position, int num_bytes, char *option)
{
  if (fp == NULL)
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }

  char expanded_name[12];
  expand_filename(filename, expanded_name);

  int found = 0;
  int file_index = -1;

  for(int i = 0; i < 16; i++)
  {
    if (strncmp(expanded_name, dir[i].DIR_Name, 11) == 0)
    {
      found = 1;
      file_index = i;
      break;
    }
  }

  if (!found)
  {
    printf("Error: File not found.\n");
    return;
  }

  int32_t cluster = dir[file_index].DIR_FirstClusterLow;
  int32_t file_size = dir[file_index].DIR_FileSize;

  if (position >= file_size)
  {
    printf("Error: Position beyond file size.\n");
    return;
  }

  //traversing to correct cluster
  while (position >= BPB_BytsPerSec * BPB_SecPerClus)
  {
    cluster = NextLB(cluster);
    if (cluster == -1)
    {
      printf("Error: Reached end of cluster chain.\n");
      return;
    }
    position -= BPB_BytsPerSec * BPB_SecPerClus;
  }

  int offset = LBAToOffset(cluster) + position;
  fseek(fp, offset, SEEK_SET);

  //read specified number of bytes
  unsigned char *buffer = malloc(num_bytes);
  if (buffer == NULL)
  {
    printf("Error: Memory allocation failed.\n");
    return;
  }
  fread(buffer, num_bytes, 1, fp);

  //byte output based on option
  for (int i = 0; i < num_bytes; i++)
  {
    if (option != NULL && strcmp(option, "-ascii") == 0)
    {
      printf("%c ", buffer[i]);
    }
    else if (option != NULL && strcmp(option, "-dec") == 0)
    {
      printf("%d ", buffer[i]);
    }
    else
    {
      printf("0x%x ", buffer[i]);
    }
  }
  printf("\n");

  free(buffer);
}

void del(char *filename)
{
  if (fp == NULL)
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }

  char expanded_name[12];
  expand_filename(filename, expanded_name);
  
  int found = 0;
  int file_index = -1;

  for (int i = 0; i < 16; i++)
  {
    if (strncmp(expanded_name, dir[i].DIR_Name, 11) == 0)
    {
      found = 1;
      file_index = i;
      break;
    }
  }

  if (!found)
  {
    printf("Error: File not found.\n");
    return;
  }

  dir[file_index].DIR_Name[0] = 0xE5; //marking found dir as deleted

  //writing the directory entry back to the image
  int offset = LBAToOffset(current_cluster) + file_index * sizeof(struct DirectoryEntry);
  fseek(fp, offset, SEEK_SET);
  fwrite(&dir[file_index], sizeof(struct DirectoryEntry), 1, fp);

  printf("File deleted.\n");
}

void undel(char *filename)
{
  if (fp == NULL)
  {
    printf("Error: File system image must be opened first.\n");
    return;
  }

  //making copy to not modify original
  char temp_filename[MAX_COMMAND_SIZE];
  strcpy(temp_filename, filename);

  char expanded_name[12];
  expand_filename(temp_filename, expanded_name);

  expanded_name[0] = 0xE5; //setting first char 0xE5 to search for deleted entry

  int found = 0;
  int file_index = -1;

  for (int i = 0; i < 16; i++)
  {
    if (strncmp(expanded_name, dir[i].DIR_Name, 11) == 0)
    {
      found = 1;
      file_index = i;
      break;
    }
  }

  if (!found)
  {
    printf("Error: Deleted file not found.\n");
    return;
  }

  //restoring first char using first char of filename provided
  dir[file_index].DIR_Name[0] = toupper(filename[0]);

  //writing directory entry back to the image
  int offset = LBAToOffset(current_cluster) + file_index * sizeof(struct DirectoryEntry);
  fseek(fp, offset, SEEK_SET);
  fwrite(&dir[file_index], sizeof(struct DirectoryEntry), 1, fp);

  printf("File undeleted.\n");
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
      if (token_count == 1)
      {
        ls(".");
      }
      else if (token_count == 2 && ((strcmp(token[1], ".") == 0) || strcmp(token[1], "..") == 0))
      {
        ls(token[1]);
      }
      else
      {
        printf("Error: Invalid arguments for ls.\n");
      }
    }
    else if (strcmp(token[0], "stat") == 0)
    {
      if (token_count != 2)
      {
        printf("Invalid command. Usage: stat <filename> (or <directory name>)\n");
      }
      else
      {
        stat(token[1]);
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
    else if (strcmp(token[0], "read") == 0)
    {
      if (token_count >= 4)
      {
        int position = atoi(token[2]);
        int num_bytes = atoi(token[3]);
        char *option = NULL;
        if (token_count == 5)
        {
          option = token[4];
        }
        read(token[1], position, num_bytes, option);
      }
      else
      {
        printf("Error: Invalid command. Usage: read <filename> <position> <number of bytes> [OPTION]\n");
      }
    }
    else if (strcmp(token[0], "del") == 0)
    {
      if (token_count != 2)
      {
        printf("Error: Invalid command. Usage: del <filename>\n");
      }
      else
      {
        del(token[1]);
      }
    }
    else if (strcmp(token[0], "undel") == 0)
    {
      if (token_count != 2)
      {
        printf("Error: Invalid command. Usage: undel <filename>\n");
      }
      else
      {
        undel(token[1]);
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