// Pierce Faraone (pf4tj)
#include "fat_internal.h"

using namespace std;

typedef struct{
    uint clusternum;
    AnyDirEntry relatedEntry;
    bool error = false;
}adeTracker;

Fat32BPB* bpb;
int fatfd;
uint global_cwd, global_root;
char* global_disk;
vector<uint>fdVec(128);
vector<uint>szVec(128);
unordered_map<uint,uint> fd2Sz;

static int debug = false;

bool fat_mount(const std::string &path) {
	void* disk;
	struct stat fileinfo;
	if ((fatfd = open(path.c_str(), O_RDONLY,0666)) < 0){;
		return false;
	}
	if (fstat(fatfd, &fileinfo) < 0 ){
		return false;
	}
	if ((disk = mmap(NULL, fileinfo.st_size, PROT_READ, MAP_PRIVATE, fatfd, 0)) == MAP_FAILED){
		return false;
	}
    global_disk = (char*)disk;
    bpb = (Fat32BPB*)disk;
    global_cwd = bpb->BPB_RootClus;
    fill(fdVec.begin(),fdVec.end(),0);
    fill(szVec.begin(),szVec.end(),0);
    return true;
}

void fat_util_print_map(unordered_map<string,AnyDirEntry> &m){
	for(const auto & pair:m){
		cout << "{" << pair.first << ": " << pair.second.dir.DIR_Name << "}\n";
	}
}

std::vector<string> fat_util_tokenize_path(const string &path){
	if (bpb == NULL) return vector<string>();
	vector <string> paths;
    string clean, part;
	clean = path;
	if (!path.empty()){
        clean = path;
    }
    if (clean[0] == '/'){
        clean.erase(0,1);
    }
    clean.erase(remove(clean.begin(),clean.end(),' '),clean.end());
    istringstream is(clean);
    while(getline(is,part,'/')){
      // cout << "token = " << part << endl;
      paths.push_back(part);
  }
  return paths;
}

std::vector<string> fat_util_upperize_tokens(vector<string> s){
	for (auto &i : s){
		for (auto &j:i){
			j = toupper(j);
		}
		if (i.length() > 2) i.erase(remove(i.begin(),i.end(),'.'),i.end());
	}
	return s;
}

int fat_util_findClusterOffset(uint32_t cn){ //get first sector of cluster
	uint32_t FirstDataSector, FirstSectorOfClusters, byteoffset;
	FirstDataSector = ((bpb->BPB_RsvdSecCnt + (bpb->BPB_NumFATs * bpb->BPB_FATSz32)));

	FirstSectorOfClusters = ((cn-2) * bpb->BPB_SecPerClus) + FirstDataSector; //sector number of first cluster

	byteoffset = FirstSectorOfClusters * bpb->BPB_BytsPerSec; 

	return byteoffset;
}
int fat_util_getNextCluster(uint32_t cn){ // updating code for next cluster
	uint32_t offset, nextClusterNum, fat_offset, fat_sector,ent_offset, off;

	int rd;

	uint32_t buffer[4];

	fat_offset = cn * 4;

	fat_sector = bpb->BPB_RsvdSecCnt + fat_offset/bpb->BPB_BytsPerSec;
	off = (fat_sector * bpb->BPB_BytsPerSec) + (fat_offset % bpb->BPB_BytsPerSec);
	if ((offset = (uint32_t)lseek(fatfd,(off_t)off,SEEK_SET)) < 0){ //cluster offset
		return - 1;
	}
	if((rd = read(fatfd,&buffer,4)) < 0){
		return -1;
	}
	nextClusterNum = *buffer & 0x0FFFFFFF;
    if (debug){
        cout << "fat_offset = " << fat_offset << endl;
        cout << "fat_sector = " << fat_sector << endl;
        cout << "ent_offset = " << ent_offset << endl;
        cout << "off = " << off << endl;
    }
    if (nextClusterNum == 0) nextClusterNum = bpb->BPB_RootClus;
    return nextClusterNum;
}

std::vector<AnyDirEntry> fat_util_readcn(uint cn){
	if (bpb == NULL) return vector<AnyDirEntry>();
	vector<AnyDirEntry> result;
	vector<string> result2;
	uint offset, n;
	uint buffer[32];
    uint bytesPerCluster = bpb->BPB_SecPerClus * bpb->BPB_BytsPerSec; //bytesPerClus
    uint clusternum = cn;
    if (cn == 0){
        cn = bpb->BPB_RootClus;
    }
    do{                                                                                                                     // for all clusters
    	n = 0;
    	if (clusternum == (uint)0x0FFFFFF7) continue;
            if ((offset = (uint)lseek(fatfd,(off_t)fat_util_findClusterOffset(clusternum),SEEK_SET)) < 0){ //cluster offset
            	return vector<AnyDirEntry>();
            }
            while(n < bytesPerCluster){
            	int rd;
            	if((rd = read(fatfd,&buffer,32)) < 0){
            		return vector<AnyDirEntry>();
            	}
            	result.push_back(*(AnyDirEntry*) buffer);
            	n += 32;
            }
            clusternum = (uint)fat_util_getNextCluster(clusternum);
        } while(clusternum <= (uint)0x0FFFFFF8);
        return result;
    }

    adeTracker fat_util_traverse_path(const std::string & path){
        adeTracker ade;
        uint32_t root;
        if (bpb == NULL){
            ade.error = true;
            return ade;
        }
        vector<string> tokens;
        // cout << "current global cwd = " << global_cwd << endl;
        // cout << "traverse path root before mutation = " << root << endl;
        if (path[0] != '/') {
            root = global_cwd;
            if (root == 0) root = bpb->BPB_RootClus;
        }else{
            root = bpb->BPB_RootClus;
        }
        // cout << "traverse path root after mutation = " << root << endl;
        tokens = fat_util_tokenize_path(path);
        tokens = fat_util_upperize_tokens(tokens);
        vector<AnyDirEntry> DirEntryVec; 
        DirEntryVec = fat_util_readcn(root);
        if (DirEntryVec.empty()){
            ade.error = true;
            return ade;
        }
        uint32_t newcn = root;
        for (const auto & tk: tokens){
            unordered_map<string,AnyDirEntry> map;
            for (const auto & entry:DirEntryVec){ 
             if (entry.dir.DIR_Name[0]==0){
                break;
            }
            if (entry.dir.DIR_Attr == 0x10 || entry.dir.DIR_Attr == 0x20 || entry.dir.DIR_Attr == 0){
                if (entry.dir.DIR_Name[0] == 0xE5) continue;
                char dir_name[12];
                dir_name[11] = 0;
                int j = 0, k = 0;
                for(j = 0; j < 11; j++){ 
                   if(entry.dir.DIR_Name[j] == ' ') continue;
                   dir_name[k++] = entry.dir.DIR_Name[j];
               }
               dir_name[k] = '\0';
               string cleanName = string(dir_name);
         // cout << "cleaned up dir name is : " << cleanName << endl;
               // cout << "string going into map = " << temp << endl;
               map.insert(make_pair(cleanName,entry));
           }
       }
       if (map.find(tk)!= map.end()){
        uint16_t clus_hi = map.at(tk).dir.DIR_FstClusHI;
         uint16_t clus_lo = map.at(tk).dir.DIR_FstClusLO;
         newcn = (clus_hi << 16) + clus_lo;
         if (newcn == 0){
            newcn = bpb->BPB_RootClus;
        }
        DirEntryVec = fat_util_readcn(newcn);
        if (DirEntryVec.empty()){
            ade.error = true;
            return ade;
        }
        if (&tk == &tokens.back()) { //last token in path
            ade.clusternum = newcn;
            ade.relatedEntry = map.at(tk);
            ade.error = false;
            return ade;
        }
    }else {
                // match not found 
        ade.error = true;
        return ade;
        // break;
    }
}
return ade;
}

bool fat_cd(const std::string &path) {
   if (bpb == NULL) return false;
   uint cn;
   adeTracker ade;
   if (path[0] == '/' && path.size() == 1){
    global_cwd = bpb->BPB_RootClus;
    return true;
}else{
    ade = fat_util_traverse_path(path);
    if (ade.error) return false;
    cn = ade.clusternum;
    if (cn == 0) cn = bpb->BPB_RootClus;
    global_cwd = cn;
    return true;
}
}

bool fat_close(int fd) {
   if (bpb == NULL) return false;
   if (fd < 0 || fd > 127) return false;
   if (fdVec[fd]== 0 ||szVec[fd] == 0) return false;
   fdVec[fd] = 0;
   szVec[fd] = 0;
   return true;
}

int fat_pread(int fd, void *buffer, int count, int offset) {
   if (bpb == NULL) return -1;
   uint cn = fdVec[fd];
   int size = (int)szVec[fd];
   int floor, address, cz;
   int bytesRead = 0;
   cz = (int) bpb->BPB_SecPerClus * bpb->BPB_BytsPerSec;
   if (fd < 0 || fd > 127) return -1;
   if (fdVec[fd] == 0 || szVec[fd] == 0) return -1;
   if (count < 0) return -1;
   if (offset < 0) return -1;
   if (count == 0) return 0;
   if (offset > size) return 0;
   if ((count + offset) > size) count = size - offset;
   int i = 0;
   while(offset >= cz){
    if (cn == 0x0FFFFFF7) break;
    // cout << "iteration = " << i << " , offset = " << offset << endl;
    offset -= cz;
    cn = fat_util_getNextCluster(cn);
    i++;
    // cout << "in pread : cn = " << cn << << endl; 
}
// cout << "in pread : cn = " << cn << << endl;
// 
address = (int)fat_util_findClusterOffset(cn);
if ((count <= (cz - offset))){
    floor = count;
}else{
    floor = cz - offset;
}
if (sizeof(buffer) != (sizeof(&global_disk[address + offset]))) return -1;
memcpy(buffer,&global_disk[address + offset],floor);
bytesRead += floor;
cn = fat_util_getNextCluster(cn);
if (cn == 0) cn = bpb->BPB_RootClus;
// specific bytes from one cluster given specific position
// read from first cluster 
// count want to read - already read. read from next cluster starting from 0 
do{
    if (cn == 0x0FFFFFF7) break;
    if (cn == 0) cn = bpb->BPB_RootClus;
    address = fat_util_findClusterOffset(cn);
    if ((count - bytesRead) <= cz){
        floor = count - bytesRead;
    }else{
        floor = cz;
    }
    // find clusterchain 
    char* target = (char*)buffer + bytesRead;
    if (sizeof((char*)buffer + bytesRead) != (sizeof(&global_disk[address]))) return -1;
    memcpy(target,&global_disk[address],floor);
    bytesRead += floor;
    cn = fat_util_getNextCluster(cn);
}while(count > bytesRead);

if (bytesRead < count) return bytesRead;
else return count;
}

    int fat_open(const std::string &path) { //only opens files.
    	if (bpb == NULL) return -1;
        adeTracker adeopen;
        uint cn, filesize;
        if (path == ""){
            cerr << "readdir error : empty string path \n";
            return -1;
        }
        else if(path[0] == '/' && path.size()==1){
            adeopen= fat_util_traverse_path(path);
            if (adeopen.error){
                return -1;
            }
            if (adeopen.relatedEntry.dir.DIR_Attr == 0x10){
                return -1;
            }
            cn = adeopen.clusternum;
            filesize = adeopen.relatedEntry.dir.DIR_FileSize;
            if (cn == 0) cn = bpb->BPB_RootClus;
            for (int fd = 0 ; fd < (int)fdVec.size();++fd){
              if (fdVec[fd] == 0) {
                 fdVec[fd] = cn;
                 szVec[fd] = filesize;
                 return fd;
             }
         }    
     }else{
        adeopen = fat_util_traverse_path(path);
        if (adeopen.error) return -1;
        if (adeopen.relatedEntry.dir.DIR_Attr == 0x10) return -1;
        uint clusternum = adeopen.clusternum;
        uint filesize = adeopen.relatedEntry.dir.DIR_FileSize;
        if (clusternum == 0) clusternum = bpb->BPB_RootClus;
        for (int fd = 0 ; fd < (int)fdVec.size();++fd){
          if (fdVec[fd] == 0) {
             fdVec[fd] = clusternum;
             szVec[fd] = filesize;
             return fd;
         }
     }
 }
 return -1;
}

std::vector<AnyDirEntry> fat_readdir(const std::string &path) {
   vector<AnyDirEntry> result;
   adeTracker ade_readdir;
   uint cn;
   if (bpb == NULL)return vector<AnyDirEntry>();
   if (path == "") {
      cerr << "readdir error : empty string path \n";
      return vector<AnyDirEntry>();
  }else if(path[0] == '/' && path.length()==1){
    ade_readdir = fat_util_traverse_path(path);
    result = fat_util_readcn(bpb->BPB_RootClus);
    if (result.empty()) return vector<AnyDirEntry>();
    else return result;
}else{
    ade_readdir = fat_util_traverse_path(path);
    if (ade_readdir.error) return vector<AnyDirEntry>();
    cn = ade_readdir.clusternum;
    // cout << "cn value in readdir = " << cn << endl;
    if (cn == 0) cn = bpb->BPB_RootClus;
    result = fat_util_readcn(cn);
    if (result.empty()) return vector<AnyDirEntry>();
    else return result;
}
}


