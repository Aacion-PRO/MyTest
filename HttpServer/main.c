#include <stdio.h>
#include "server.h"

// ԭ��main����ֻ���߼��������ã���������ݲ���д��������
// ���ܺ���������Ҫ�����ܵ�һ��һ�������Ĵ���һ�����Ҳ��д��ʮ��
int main(int argc,char *argv[]) {
	// a.out port path ��������Դ��Ŀ¼
	if (argc < 3) {
		printf("./a.out port respath\n");
		exit(0);
	}
	// ��Դ��Ŀ¼��ֵ� argv[2]�����裺/home/robin/luffy
	// ����ǰ�������Ľ��̹���Ŀ¼�л�����ԴĿ¼��
	chdir(argv[2]);
	// ���������� -> ����epoll
	unsigned short port = atoi(argv[1]);
	epollRun(port);

	return 0;
}
