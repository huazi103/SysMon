#pragma once
//�¼�������

enum class ItemType : short {
	None,
	ProcessCreate,
	ProcessExit,
	ThreadCreate,
	ThreadExit,
	ImageLoad
};

//�¼��Ĺ�����Ϣ
struct ItemHeader {
	ItemType Type;
	USHORT Size;//���ɴ�С
	LARGE_INTEGER Time;//�¼���ʱ��
};

//�����˳���ֻ���˳��Ľ��̵�ID����Ȥ
struct ProcessExitInfor : ItemHeader {
	ULONG ProcessId;
};

//�˳����̽ṹ����Ϣ
struct ProcessExitInfo :ItemHeader {
	ULONG ProcessId;
};


//�������̽ṹ����Ϣ
struct ProcessCreateInfo : ItemHeader {
	ULONG ProcessId;//����ID
	ULONG ParentProcessId;//�����Ľ��̵ĸ�����ID
	USHORT CommandLineLength;//�������ַ�������
	USHORT CommandLineOffset;//�������ַ����ӽṹ��ʼ����ʼ��ƫ����
};

struct ThreadCreateExitInfo : ItemHeader {
	ULONG ThreadId;//�߳�ID
	ULONG ProcessID;//�̶߳�Ӧ�Ľ���ID

};
const int MaxImageFileSize = 300;

struct ImageLoadInfo : ItemHeader {
	ULONG ProcessId;//����ID
	void* LoadAddress;//���ص�ģ���׵�ַ
	ULONG_PTR ImageSize;//ģ���С
	WCHAR ImageFileName[MaxImageFileSize + 1];//ģ���ļ���
};