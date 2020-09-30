#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#ifdef __linux__
	#include <linux/fs.h>
#elif defined(__APPLE__)
	#define O_DIRECT	F_NOCACHE
	#define FIGETBSZ	0
	#define FIBMAP		0
#endif

#include "linkedlist.h"

extern int errno;

#define dbgprint	(1)
#define BILLION 	(1E9)

uint64_t hole = ~0ULL;
long maxfileread=10;

long totread;	// bytes
long totfilecnt;	// file cnt
char *buffer;

struct file_extent {
	struct file_info*	f_info;
	uint64_t			sequence;
	uint64_t 			first_block;
	uint64_t 			last_block;
	uint64_t 			block_count;
	struct list_head	list;
};

struct file_info {
	char* 				name;
	uint64_t 			size;
	uint64_t			blksize;
	uint64_t			blkcnt;
	uint64_t			extcnt;
	struct file_extent*	exthead;
	struct list_head	list;
};

LIST_HEAD(fi_listhead);
LIST_HEAD(fext_listhead);

void list_sort(void *priv, struct list_head *head,
		int (*cmp)(void *priv, struct list_head *a,
			struct list_head *b));


int fibmap(const char* fname);
int listdir(const char *name, int indent)
{
	int err=0;
    DIR *dir;
    struct dirent *entry;
	char filepath[512];
    if (!(dir = opendir(name))){
		printf("%s is not dir\n", name);
		return 1;
	}
    while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_DIR) {
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
				continue;
			snprintf(filepath, sizeof(filepath), "%s/%s", name, entry->d_name);
			if(dbgprint)
				printf("%*s[%s]\n", indent, "", filepath);
			err=listdir(filepath, indent + 2);
			if(err>0)
				break;
		} else {
			snprintf(filepath, sizeof(filepath), "%s/%s", name, entry->d_name);
			if(dbgprint)
				printf("%*s- %s\n", indent, "", filepath);
			if(fibmap(filepath)>0){
				printf("error bitmap\n");
				err=2;
				break;
			}
			if(++totfilecnt>=maxfileread){
				printf("reach to max\n");
				err=3;
				break;
			}
        }
    }
    closedir(dir);
	return err;
}


#if 0
void readofst(const char* tgtdrv){
	int fd;
	long i;
	fd=open(tgtdrv,O_DIRECT);
	if(fd<0){
		printf("fail to open sda1\n");
		return;
	}
	long startbyte, length;
	long blksize = 4096;
	long readsize;
	long j;
	for(i=0;i<ofstlist_idx;i++){
		startbyte = ofstlist[i].first_block*blksize;
		length = ofstlist[i].block_count;
		if(dbgprint)
			printf("%ld - len: %ld read\n",startbyte,length*blksize);
		lseek(fd,startbyte,SEEK_SET);
		for(j=0;j<length;j++){
			readsize = read(fd,buffer,blksize);
			totread += readsize;
			if(readsize<blksize){
				printf("fail to read 4096b from %ld\n",startbyte+j*512);
				break;
			}
		}
		if(j<length){
			break;
		}
	}
	close(fd);
}
#endif


int fibmap(const char* fname)
{
	int fd;
	struct stat st;
	int err = 0;
	fd = open(fname, O_RDONLY);
	if (fd <= 0) {
		perror("error opening file");
		return 1;
	}
	if (fstat(fd, &st)) {
		perror("fstat error");
		close(fd);
		return 3;
	}
	if(st.st_size==0){
		perror("size is 0");
		close(fd);
		return 4;
	}
	struct file_info* f_entry = (struct file_info*)malloc(sizeof(struct file_info)); 
	f_entry->name = (char*)malloc(strlen(fname)+10);
	strcpy(f_entry->name,fname);
	f_entry->blksize = st.st_blksize;
	f_entry->size = st.st_size;
	f_entry->blkcnt = (st.st_size + st.st_blksize - 1) / st.st_blksize;
	if(dbgprint){
		printf("File %s size %d blocks %d blocksize %d\n",
			fname, (int)st.st_size, f_entry->blkcnt, st.st_blksize);
	}
	list_add_tail(&f_entry->list, &fi_listhead);

	struct file_extent* ext = (struct file_extent*)malloc(sizeof(struct file_extent));
	memset(ext, 0, sizeof(struct file_extent));
	ext->f_info = f_entry;
	list_add_tail(&ext->list, &fext_listhead);
	f_entry->exthead = ext;
	f_entry->extcnt = 1;

	uint64_t blk_idx, blknum64, block;
	for (blk_idx = 0; blk_idx < f_entry->blkcnt; blk_idx++) {
		block = blk_idx;
		if (ioctl(fd, FIBMAP, &block)) {
			perror("FIBMAP ioctl failed");
			err=4;
			break;
		}
		blknum64 = block;
		if (blk_idx && blknum64 == (ext->last_block + 1)) {
			ext->last_block = blknum64 ? blknum64 : hole;
			ext->block_count++;
		} else {
			uint64_t lastblock = ext->last_block;
			if (blk_idx){
				struct file_extent* new_ext = (struct file_extent*)malloc(sizeof(struct file_extent));
				new_ext->f_info = f_entry;
				new_ext->sequence = ext->sequence+1;
				list_add_tail(&new_ext->list, &fext_listhead);
				f_entry->extcnt++;
				ext = new_ext;
			}
			//ext->first_block = blknum64;
			ext->first_block = blknum64 ? blknum64 : (lastblock? lastblock+1 : blknum64);
			ext->last_block  = blknum64 ? blknum64 : hole;
			ext->block_count = 1;
		}
	}
	close(fd);
	return err;
}


void print_fext(struct file_extent* fext){
	printf("f:%llu l:%llu e:%llu\n", 
			fext->first_block, fext->block_count, fext->last_block);
}


void print_finfo(struct file_info* finfo){
	printf("name:%s %llubytes (blksize: %llu / blkcnt: %llu/ excnt: %llu)\n", 
			finfo->name, finfo->size, finfo->blksize, finfo->blkcnt, finfo->extcnt);
	printf("  ");
	print_fext(finfo->exthead);
}


static int file_ext_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct file_extent *rqa = list_entry(a, struct file_extent, list);
	struct file_extent *rqb = list_entry(b, struct file_extent, list);
	return rqa->first_block - rqb->first_block;
}


int main(int argc, char* argv[]){
	struct timespec requestStart, reqmid, requestEnd;
	double elp;
	totread=0;
	totfilecnt=0;
	buffer = aligned_alloc(4096, 256*1024);
	if(!buffer){
		printf("error on alloc memory\n");
		return -1;
	}
	printf("Start reading %s\n",argv[1]);

	// list up dir
	clock_gettime(CLOCK_REALTIME, &requestStart);
	listdir(argv[1],0);

	clock_gettime(CLOCK_REALTIME, &reqmid);
	elp = ( reqmid.tv_sec - requestStart.tv_sec )
			+ ( reqmid.tv_nsec - requestStart.tv_nsec ) / BILLION;

	// sort ofst
	list_sort(NULL, &fext_listhead, file_ext_cmp);
	printf("collecting done %.2fs read start\n",elp);

	// read ofst

	clock_gettime(CLOCK_REALTIME, &requestEnd);
	elp = ( requestEnd.tv_sec - reqmid.tv_sec )
			+ ( requestEnd.tv_nsec - reqmid.tv_nsec ) / BILLION;

	printf("Ending %.2fs for reading files %ld(%ld bytes)\n",elp,totfilecnt,totread);
	free(buffer);

	struct list_head *next, *temp;
	list_for_each_safe(next, temp, &fi_listhead){
		struct file_info* del_fi = list_entry(next, struct file_info, list);
		//print_finfo(del_fi);
		if(del_fi->name)
			free(del_fi->name);
		free(del_fi);
	}
	list_for_each_safe(next, temp, &fext_listhead){
		struct file_extent* del_ext = list_entry(next, struct file_extent, list);
		print_fext(del_ext);
		free(del_ext);
	}
	return 0;
}
