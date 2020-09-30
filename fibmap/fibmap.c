#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <inttypes.h>
#include <memory.h>
#ifdef __linux__
	#include <linux/fs.h>
#elif defined(__APPLE__)
	#define O_DIRECT	F_NOCACHE
	#define FIGETBSZ	0
	#define FIBMAP		0
#endif

int 		comp = 0;
uint64_t	hole = ~0ULL;

char 		buffer[4096];
char 		compbuff[4096];
long 		tot_compbyte=0;
long 		tot_readbyte=0;
long 		tot_writebyte=0;

struct file_extent {
	uint64_t first_block;
	uint64_t last_block;
	uint64_t block_count;
};

struct file_desp {
	int fd_src;
	int fd_disk;
	int fd_dest;
	blksize_t blksize;
	off_t filesize;
};

void handleExt(struct file_extent ext){
	if(ext.last_block==hole)
		return;
	printf("sblk %llu", ext.first_block);
	printf(" eblk %llu", ext.last_block);
	printf(" blkcnt %llu(sector %llu)\n", ext.block_count, ext.block_count*8);
}

int readWrite(struct file_extent *ext, struct file_desp *fd){
	if(fd->fd_disk<=0 || fd->fd_dest<=0 || fd->fd_src<=0)
		return 0;

	int blksize = (int)fd->blksize;
	if(ext->last_block==hole)
		memset(buffer,0,sizeof(char)*blksize);
	else
		lseek(fd->fd_disk, ext->first_block*blksize, SEEK_SET);

	int readbyte = blksize;
	uint64_t cnt=0;
	for(cnt=0; cnt<ext->block_count; cnt++){
		if(fd->filesize<=0){
			printf("something wrong on data size");
			return 4;
		}
		if(ext->last_block!=hole)
			readbyte = read(fd->fd_disk, buffer, sizeof(char)*blksize);
		if(readbyte <= 0)
			return 1;
		readbyte = (fd->filesize < readbyte)? fd->filesize: readbyte;
		fd->filesize -= readbyte;
		tot_readbyte += readbyte;
		if(comp){
			int comp_byte = read(fd->fd_src, compbuff, sizeof(char)*readbyte);
			if(memcmp(compbuff,buffer,sizeof(char)*comp_byte)!=0){
				printf("diff data offset %ld\n",tot_compbyte);
				return 2;
			}
			tot_compbyte += comp_byte;
		}
		int writebyte = write(fd->fd_dest,buffer,sizeof(char)*readbyte);
		if(writebyte != readbyte)
			return 3;
		tot_writebyte += writebyte;
	}
	return 0;
}


int main(int argc, char **argv)
{
	int err = 0;

	// open related files
	struct file_desp fd;
	memset(&fd, 0, sizeof(fd));
	fd.fd_src = open(argv[1], O_RDONLY);
	if (fd.fd_src <= 0) {
		perror("error opening source file");
		err = 1;
		goto end;
	}
	if(argc>3){
		fd.fd_dest = open(argv[2], O_WRONLY | O_CREAT);
		if(fd.fd_dest<=0){
			perror("error opening dest file");
			err = 2;
			goto end;
		}
		fd.fd_disk = open(argv[3], O_RDONLY);
		if(fd.fd_disk<=0){
			perror("error opening source disk");
			err = 3;
			goto end;
		}
	}

	// get file stat
	struct stat st;
	if (fstat(fd.fd_src, &st)) {
		perror("fstat error");
		err = 4;
		goto end;
	}
	fd.filesize = st.st_size;
	fd.blksize = st.st_blksize;
	if(fd.blksize<512){
		perror("blocksize is less than 512");
		err = 5;
		goto end;
	}
	uint64_t blkcnt = (st.st_size + fd.blksize - 1) / fd.blksize;
	printf("File %s size %d blocks %d blocksize %d\n",
			argv[1], (int)fd.filesize, blkcnt, fd.blksize);

	// get block offset
	struct file_extent ext;
	memset(&ext, 0, sizeof(ext));
	uint64_t blkidx, block, blknum64;
	for (blkidx = 0; blkidx < blkcnt; blkidx++) {
		block = blkidx;
		if (ioctl(fd.fd_src, FIBMAP, &block)) {
			perror("FIBMAP ioctl failed");
			err = 6;
			break;
		}
		blknum64 = block;
		if (blkidx && blknum64 == (ext.last_block + 1)) {
			ext.last_block = blknum64 ? blknum64 : hole;
			ext.block_count++;
		} else {
			if (blkidx){
				handleExt(ext);
				if(err = readWrite(&ext, &fd)>0)
					break;
			}
			ext.first_block = blknum64;
			ext.last_block  = blknum64 ? blknum64 : hole;
			ext.block_count = 1;
		}
	}
	if(err==0){
		handleExt(ext);
		err = readWrite(&ext, &fd);
	}
	printf("%ld read/ %ld write\n", tot_readbyte, tot_writebyte);

end:
	close(fd.fd_disk);
	close(fd.fd_dest);
	close(fd.fd_src);
	return err;
}
