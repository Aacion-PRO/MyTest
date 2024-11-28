#include "server.h"
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>
int initListenFd(unsigned short port) {
	// 1. �����������׽���
	int lfd = socket(AF_INET,SOCK_STREAM,0);
	if (lfd == -1) {
		perror("socket");
		return -1;
	}

	// 2. ���ö˿ڸ���
	int opt = 1;
	int ret = setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
	if (ret == -1) {
		perror("setsockopt");
		return -1;
	}
	// 3. ��
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	ret = bind(lfd,(struct sockaddr*)&addr,sizeof(addr));
	if (ret == -1) {
		perror("bind");
		return -1;
	}
	// 4. ���ü���
	ret = listen(lfd,128);
	if (ret == -1) {
		perror("listen");
		return -1;
	}
	// 5. ���õ��Ŀ����׽��ַ��ظ�������
	return lfd;
}
int epollRun(unsigned short port) {
	// 1. ����epollģ��
	int epfd = epoll_create(10);
	if (epfd == -1) {
		perror("epoll_create");
		return -1;
	}
	// 2. ��ʼ��epollģ��
	int lfd = initListenFd(port);
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = lfd;
	// ��� lfd �����ģ����
	int ret = epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
	if (ret == -1) {
		perror("epoll_ctl");
		return -1;
	}
	// ��� - ѭ�����
	struct epoll_event evs[1024];
	int size = sizeof(evs) / sizeof(evs[0]);
	int flag = 0;
	while (1) {
		if (flag) {
			break;
		}
		// ���̲߳�ͣ�ĵ���epoll_wait
		int num = epoll_wait(epfd,evs,size,-1);
		for (int i = 0; i < num; ++i)
		{
			int curfd = evs[i].data.fd;
			if (curfd == lfd) {
				// ����������
				// �����̣߳������߳̽���������
				// acceptConn�����̵߳Ļص�
				int ret = acceptConn(lfd,epfd);
				if (ret == -1) {
					// �涨����������ʧ�ܣ�ֱ����ֹ����
					int flag = 1;
					break;
				}
			}
			else {
				// ͨ�� -> �Ƚ������ݣ�Ȼ��ظ�����
				// �������̣߳������߳���ͨ�ţ�recvHttpRequest�����̵߳Ļص�
				recvHttpRequest(curfd,epfd);
			}
		}
	}
	return 0;
}

int acceptConn(int lfd, int epfd)
{	
	// 1. ����������
	int cfd = accept(lfd,NULL,NULL);
	if (cfd == -1) {
		perror("accept");
		return -1;
	}
	// 2. ����ͨ��������Ϊ������
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);

	// 3. ͨ�ŵ��ļ���������ӵ�epollģ����
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLET; // ����ģʽ
	ev.data.fd = cfd;
	int ret = epoll_ctl(epfd,EPOLL_CTL_ADD,cfd,&ev);
	if (ret == -1) {
		perror("epoll_ctl");
		return -1;
	}
	return 0;
}

int recvHttpRequest(int cfd,int epfd) 
{	
	// ��Ϊ�Ǳ��ط�����ģʽ������ѭ��������
	char tmp[1024]; // ÿ�ζ�1k����
	char buf[4096]; // ��ÿ�ζ��������ݴ洢�����buf��
	
	// ѭ��������
	int len,total = 0; // total:��ǰbuf���Ѿ��洢�˶�������
	// û�б�Ҫ�����е�http����ȫ����������
	// ��Ϊ��Ҫ�����ݶ�����������
	// - �ͻ���������������Ǿ�̬��Դ���������Դ�����������еĵڶ�����
	// - ֻ��Ҫ�����������ı��������Ϳ��ã������к������ͷ�Ϳ���
	// - ����Ҫ��������ͷ�е����ݣ���˽��յ� ֮�󲻴洢Ҳ��û�����
	while ((len = recv(cfd, tmp, sizeof(tmp), 0)) > 0) {
		if (total + len < sizeof(buf)) {
			// �пռ�洢����
			memcpy(buf+total,tmp,len);
		}
		total += len;
	}
	//printf("%s\n",buf);
	// ѭ������ -> ������
	// �������Ƿ������ģ���ǰ������û�����ݷ���-1��errno == EAGAIN
	if(len == -1 && errno == EAGAIN){
		// �������дӽ��յ��������ó���
		// ��httpЭ���л���ʹ�õ��� /r/n
		// �����ַ�������������һ��/r/n��ʱ����ζ�������о��õ���
		char* pt = strstr(buf,"\r\n");
		// �������ֽ���(����)
		int reqlen = pt - buf;
		// ����������
		buf[reqlen] = "\0";// �ַ����ض�
		// ����������
		parseRequestLine(cfd,buf);
	}
	else if( len == 0){
		printf("�ͻ��˶Ͽ�������...\n");
		// �������Ϳͻ��˶Ͽ����ӣ��ļ���������epollģ����ɾ��
		disConnect(cfd,epfd);
	}
	else {
		perror("recv");
	}
	return 0;
}

int parseRequestLine(int cfd,const char* reqLine)
{
	// �����з�Ϊ������
	// GET /hello/word http/1.1
	// 1.�������е����������˲𿪣����õ�ǰ������
	// - �ύ���ݵķ�ʽ
	// - �ͻ����������������ļ���
	char method[6];
	char path[1024];
	sscanf(reqLine,"%[^ ] %[^ ]",method,path);
	//printf("method = %s , path = %s\n",method,path);

	// 2. �ж�����ʽ�ǲ���get������getֱ�Ӻ���
	// http�в����ִ�Сд get / GET / Get
	if (strcasecmp(method,"get") != 0) {
		printf("�û��ύ����get���󣬺���...\n");
		return -1;
	}
	// 3. �ж��û��ύ��������Ҫ���ʷ��������ļ�����Ŀ¼
	// /hello/world/
	// - ��һ�� / ������������Դ��Ŀ¼
	// - hello/world/ -> ��������Դ��Ŀ¼�е�����Ŀ¼
	// ��Ҫ�ڳ������жϵõ��ļ������� - stat()
	// �ж�path�д洢�ĵ�����ʲô�ַ�����
	char* file = 0;//file�б�����ļ�·���������·��

	// ����ļ����������ģ���Ҫ��ԭ
	decodeMsg(path, path);

	if (strcmp(path, "/") == 0) {
		// ���ʵ��Ƿ�������Դ��Ŀ¼ /home/robin/luffy
		// / ����ϵͳ��Ŀ¼���Ƿ������ṩ����Ŀ¼ == ���ݽ��е� /home/robin/luffy
		// ����ڷ�����������������Դ��Ŀ¼����������
		// - �����������������ʱ����ָ����Դ��Ŀ¼���Ǹ�Ŀ¼
		// - ��main�����н�����Ŀ¼�л�������Դ��Ŀ¼ /home/robin/luffy
		// - ������ ./ == /home/robin/luffy
		file = "./"; // ��Ӧ��Ŀ¼���ǿͻ��˷��ʵ���Դ�ĸ�Ŀ¼
	}
	else {
		// �����������ģ�/home/a.txt
		// ./ == /home/robin/luffy ===> /home/robin/luffy/hello/a.txt
		// ������� / ȥ�����൱��Ҫ���ʵ�ϵͳ�ĸ�Ŀ¼
		file = path + 1;// hello/a.txt == ./hello/a.txt
	}
	printf("�ͻ���������ļ�����%s\n",file);

	// �����ж�
	struct stat st;
	int ret = stat(file,&st);
	if (ret == -1) {
		// �ж��ļ�����ʧ�� ==> û������ļ�
		// ���ͻ��˷���404ҳ��
		sendHeadMsg(cfd,404,"Not Found",getFileType("html"), -1);
		sendFile(cfd,"404.html");
	}

	if (S_ISDIR(st.st_mode)) {
		// ����Ŀ¼����Ŀ¼�����ݷ��͸��ͻ���
		// 5. �ͻ��������������һ��Ŀ¼������Ŀ¼�����ļ����ݸ����ͻ���
		sendHeadMsg(cfd,200,"OK", getFileType("html"),-1);
		sendDir(cfd,file); //<table></table>

	}
	else{
		// ������ļ����Ͱ��ļ����ݷ��͸��ͻ���
		// 4. �ͻ��������������һ���ļ��������ļ����ݸ����ͻ���
		sendHeadMsg(cfd,200,"OK",getFileType(file),st.st_size);
		sendFile(cfd,file);
		printf("file = %s\n", file);
	}
	
	return 0;
}
/*
	status:״̬��
	descr:״̬����
	type:Content-Type ��ֵ��Ҫ�ظ����ݵ�����
	length:Content-length ��ֵ,Ҫ�ظ����ݵĳ���
*/ 
int sendHeadMsg(int cfd,int status,const char* descr,const char* type,int length)
{	
	// ״̬�� + ��Ϣ��ͷ + ����
	char buf[4096];
	// http/1.1 200 OK
	sprintf(buf,"http/1.1 %d %s\r\n",status,descr);
	// ��Ϣ��ͷ -> 2����ֵ��
	// content-type:xxx
	sprintf(buf + strlen(buf), "Content-Type: %s\r\n",type);
	// .mp3 ==> audio.
	// content-length:111
	// ����
	sprintf(buf + strlen(buf), "Content-Length: %d\r\n\r\n", length);
	// ƴ�����֮�󣬷���
	send(cfd, buf, strlen(buf),0);
	//printf("sendHeadMsg buf = %s\n", buf);
	return 0;
}

int sendFile(int cfd,const char* fileName)
{
	// �ڷ�������֮ǰӦ���� ״̬��+��Ϣ��ͷ+����+�ļ�����
	// ���Ĳ���������Ҫ��֯��֮���ٷ�����?
	// - ����Ҫ��Ϊʲô�� -> �������Ĭ��ʹ�õ�tcp
	// �������ӵ���ʽ����Э�� -> ֻ�����ȫ��������Ϳ���
	// ���ļ����ݣ����͸��ͻ���
	// ���ļ�
	int fd = open(fileName,O_RDONLY);
	// ѭ�����ļ�
	while (1) {
		char buf[1024] = { 0 };
		int len = read(fd,buf,sizeof(buf));
		if (len > 0) {
			// ���Ͷ������ļ�����
			send(cfd,buf,len,0);
			// ���Ͷ˷���̫�쵼�½��ն˵���ʾ���쳣
			usleep(50);
		}
		else if(len == 0){
			break;
		}
		else {
			perror("���ļ�ʧ����...\n");
			return -1;
		}
	}
	return 0;
}

/*
	�ͻ��˷���Ŀ¼����������Ҫ������ǰĿ¼�����ҽ�Ŀ¼�е������ļ����͸��ͻ���
	- ����Ŀ¼�õ����ļ�����Ҫ�ŵ�html�ı����
	- �ظ��ĸ�ʽ��html��ʽ�����ݿ�
	<html>
		<head>
			<title>test</title>
		</head>
		<body>
			<table>
				<tr>
					<td>�ļ���</td>
					<td>�ļ���С</td>
				</tr>
			<table>
		</body>
	</html>
*/
// opendir readdir closedir
int sendDir(int cfd, const char* dirName)
{	
	char buf[4096];
	struct dirent** namelist;
	sprintf(buf,"<html><head><title>%s</title></head><body><table>",dirName);
	int num = scandir(dirName,&namelist,NULL, alphasort);
	for (int i = 0; i < num; ++i) {
		// ȡ���ļ���
		char* name = namelist[i]->d_name;
		// ƴ�ӵ�ǰ�ļ�����Դ�ļ��е����·��
		char subpath[1024];
		sprintf(subpath,"%s/%s",dirName,name);
		struct stat st;
		// stat�����ĵ�һ�������ļ���·��
		stat(subpath,&st);
		if (S_ISDIR(st.st_mode)) {
			// �����Ŀ¼�������ӵ���ת·���ļ�������� /(б��)
			sprintf(buf+strlen(buf),"<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",name,name,(long)st.st_size);
		}
		else {
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>", name, name, (long)st.st_size);
		}
		// ��������
		send(cfd,buf,strlen(buf),0);
		// �������
		memset(buf,0,sizeof(buf));
		//�ͷ���Դ namelist[i] ���ָ��ָ��һ����Ч���ڴ�
		free(namelist[i]);
	}
	// ��ȫhtml��ʣ��ı�ǩ
	sprintf(buf,"</table></body></html>");
	send(cfd,buf,strlen(buf),0);
	// �ͷŶ���ָ�� namelist
	free(namelist);
	return 0;
}

int disConnect(int cfd, int epfd)
{
	// ��cfd��epollģ����ɾ��
	int ret = epoll_ctl(epfd,EPOLL_CTL_DEL,cfd,0);
	if (ret == -1) {
		perror("epoll_ctl");
		close(cfd);
		return -1;
	}
	close(cfd);

	return 0;
}

// ͨ���ļ�����ȡ�ļ�������
// ������name -> �ļ���
// ����ֵ������ļ���Ӧ��content-type������
const char *getFileType(const char* name) {
	// a.jpg a.mp4 a.html
	// ����������ң�'.' �ַ����粻���ڷ���NULL
	if (strcmp(name, "html") == 0)
		return "text/html; charset=utf-8";
	const char* dot = strrchr(name,".");
	char a[20],b[20];
	sscanf(name, "/%[0-9,a-z,A-Z].%[0,9,a-z,A-Z]",a,b);
	
	//printf("a = %s,b = %s\n",a,b);

	//printf("name = %s\n",name);
	//printf("dot = %s\n", dot);

	if (b == NULL)
		return "text/plain; charset=utf-8";//���ı�
	if (strcmp(b, "html") == 0 || strcmp(b, "htm") == 0)
		return "text/html; charset=utf-8";
	if (strcmp(b, "jpg") == 0 || strcmp(b, "jpeg") == 0)
		return "image/jpeg";
	if (strcmp(b, "gif") == 0)
		return "image/gif";
	if (strcmp(b, "png") == 0)
		return "image/png";
	if (strcmp(b, "css") == 0)
		return "text/css";
	if (strcmp(b, "au") == 0)
		return "audio/basic";
	if (strcmp(b, "wav") == 0)
		return "audio/wav";
	if (strcmp(b, "avi") == 0)
		return "video/x-msvideo";
	if (strcmp(b, "mov") == 0 || strcmp(b, "qt") == 0)
		return "video/quicktime";
	if (strcmp(b, "mpeg") == 0 || strcmp(b, "mpe") == 0)
		return "video/mpeg";
	if (strcmp(b, "vrml") == 0 || strcmp(b, "wrl") == 0)
		return "model/vrml";
	if (strcmp(b, ".midi") == 0 || strcmp(b, "mid") == 0)
		return "audio/midi";
	if (strcmp(b, "mp3") == 0)
		return "audio/mpeg";
	if (strcmp(b, "ogg") == 0)
		return "application/ogg";
	if (strcmp(b, "pac") == 0)
		return "application/x-ns-proxy-autoconfig";
	
	return "text/plain; charset=utf-8";
}


// ���յõ�10���Ƶ�����
int hexit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return 0;
}

// ����
// from: Ҫ��ת�����ַ� -> �������
// to��ת��֮��õ����ַ� -> ��������
void decodeMsg(char* to, char* from) {
	for (; *from != '\0'; ++to, ++from)
	{
		// isxdigit -> �ж��ַ����ǲ���16���Ƹ�ʽ
		// Linux%E5%86%85%E6%A0%B8.jpg
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {
			// ��16���Ƶ��� -> ʮ���� �������ֵ��ֵ�����ַ� int -> char
			// A1 = 161
			*to = hexit(from[1]) * 16 + hexit(from[2]);

			from += 2;
		}
		else {
			// ���������ַ�ֱ�Ӹ�ֵ
			*to = *from;
		}
	}
	*to = '\0';
}