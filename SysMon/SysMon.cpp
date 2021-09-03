#include"pch.h"
#include"SysMon.h"
#include"AutoLock.h"

Globals g_Globals;	//ȫ�ֱ������������еĻ���


extern "C" NTSTATUS 
DriverEntry(PDRIVER_OBJECT DriverObject,PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	auto status = STATUS_SUCCESS;
	InitializeListHead(&g_Globals.ItemHead);//��ʼ������
	g_Globals.Mutex.Init();		//��ʼ��������

	//�����豸����ͷ�������
	PDEVICE_OBJECT DeviceObject = NULL;
	UNICODE_STRING symLinkName = RTL_CONSTANT_STRING(L"\\??\\sysmon");
	bool symLinkCreate = FALSE;
	do {
		UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\sysmon");
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, TRUE, &DeviceObject);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("failed to create device  Error:(0x%08X)",status));
			break;
		}
		DeviceObject->Flags |= DO_DIRECT_IO;//ֱ��IO

		status = IoCreateSymbolicLink(&symLinkName, &devName);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("failed to create SymbolcLink Error:(0x%08X)\n",status));
			break;
		}
		symLinkCreate = TRUE;

		//ע��������Ѻ���
		status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("failed to register process callback (0x%08X)\n",status));
			break;
		}

		//ע���߳����Ѻ���
		status = PsSetCreateThreadNotifyRoutine(OnThreadNotiry);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("failed to register thread callback (0x%08X)\n", status));
			break;
		}

		//ע�����ģ�����Ѻ���
		status = PsSetLoadImageNotifyRoutine(PloadImageNotifyRoutine);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("failed to register LoadImage callback (0x%08X)\n", status));
			break;
		}
	} while (false);

	if (!NT_SUCCESS(status))
	{
		if (symLinkCreate)
			IoDeleteSymbolicLink(&symLinkName);
		if (DeviceObject)
			IoDeleteDevice(DeviceObject);
	}


	DriverObject->DriverUnload = SysMonUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = SysMonCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = SysMonRead;

	return status;
}

NTSTATUS SysMonCreateClose(IN PDEVICE_OBJECT pDevObj, IN PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDevObj);
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, 0);
	return 0;
}
NTSTATUS SysMonRead(IN PDEVICE_OBJECT pDevObj, IN PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDevObj);
	auto stack = IoGetCurrentIrpStackLocation(pIrp);
	auto len = stack->Parameters.Read.Length;//��ȡUser�Ķ�ȡ��������С
	auto status = STATUS_SUCCESS;
	auto count = 0;
	NT_ASSERT(pIrp->MdlAddress);//MdlAddress��ʾʹ����ֱ��I/O

	auto buffer = (UCHAR*)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, NormalPagePriority);//��ȡֱ��I/O��Ӧ���ڴ�ռ仺����
	if (!buffer)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	else
	{
		//��������ͷ����ȡ���ݷ��ظ�User,������ݺ��ֱ��ɾ��
		AutoLock<FastMutex> lock(g_Globals.Mutex);
		while (TRUE)
		{
			if (IsListEmpty(&g_Globals.ItemHead))//�������Ϊ�վ��˳�ѭ������Ȼ���ItemCountҲ�ǿ��Ե�
			{
				break;//�˳�ѭ��
			}
			auto entry = RemoveHeadList(&g_Globals.ItemHead);
			auto info = CONTAINING_RECORD(entry,FullItem<ItemHeader>, Entry);//�����׵�ַ
			auto size = info->Data.Size;
			if (len < size)
			{
				//ʣ�µ�BUFFER������
				//�ַŻ�ȥ
				InsertHeadList(&g_Globals.ItemHead, entry);
				break;
			}
			g_Globals.ItemCount--;
			::memcpy(buffer, &info->Data, size);
			len -= size;
			buffer += size;
			count += size;

			//�ͷ��ڴ�
			ExFreePool(info);
		}
	}
	//��ɴ˴�
	pIrp->IoStatus.Status = status;
	pIrp->IoStatus.Information = count;
	IoCompleteRequest(pIrp, 0);
	return status;
}
void PushItem(LIST_ENTRY* entry)
{
	AutoLock<FastMutex> lock(g_Globals.Mutex);//���ٻ�ȡ������
	if (g_Globals.ItemCount > 1024)
	{
		//̫����̵��˳��ʹ����¼���Ҫɾ��һЩ
		auto head = RemoveHeadList(&g_Globals.ItemHead);
		//������������Ƴ�������ֵ�������ָ��
		g_Globals.ItemCount--;

		auto item = CONTAINING_RECORD(head, FullItem<ItemHeader>, Entry);
		//��ȡ�Ƴ����Ľṹ����׵�ַ����Ϊ�п��ܽṹ�����entry�������ڵ�һ��
		ExFreePool(item);//�ͷ��ڴ�
	}
	InsertTailList(&g_Globals.ItemHead, entry);//���뵽������
	g_Globals.ItemCount++;
}



void OnProcessNotify(PEPROCESS Process,HANDLE ProcessId,PPS_CREATE_NOTIFY_INFO CreateInfo)
{
	UNREFERENCED_PARAMETER(Process);
	//������̱�����CreateInfo�������ΪNULL
	if (CreateInfo)
	{
		//���̴����¼���ȡ����

		USHORT allocSize = sizeof(FullItem<ProcessCreateInfo>);
		USHORT commandLineSize = 0;
		if (CreateInfo->CommandLine)//���������������
		{
			commandLineSize = CreateInfo->CommandLine->Length;
			allocSize += commandLineSize;//Ҫ������ڴ��С
		}
		auto info = (FullItem<ProcessCreateInfo>*)ExAllocatePoolWithTag(PagedPool, allocSize, DRIVER_TAG);
		if (info == nullptr)
		{
			KdPrint(("SysMon: When process is creating,failed to allocate memory"));
			return;
		}
		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::ProcessCreate;
		item.Size = allocSize;
		item.ProcessId = HandleToULong(ProcessId);
		item.ParentProcessId = HandleToULong(CreateInfo->ParentProcessId);
		
		if (commandLineSize > 0)
		{
			::memcpy((UCHAR*)&item+sizeof(item),CreateInfo->CommandLine->Buffer,commandLineSize);//�������е����ݸ��Ƶ����ٵ��ڴ�ռ����
			item.CommandLineLength = commandLineSize / sizeof(WCHAR);//��wcharΪ��λ
			item.CommandLineOffset = sizeof(item);//�Ӷ�ÿ�ʼƫ���������ַ������׵�ַ
		}
		else
		{
			item.CommandLineLength = 0;
			item.CommandLineOffset = 0;
		}
		PushItem(&info->Entry);
	}
	else
	{
		//�����˳�
	
		//�����˳��Ľ��̵�ID���¼��Ĺ���ͷ��,ProcessExitInfo�Ƿ�װ��ר������˳����̱������Ϣ�ṹ��,DRIVER_TAG�Ƿ�����ڴ�ı�ǩλ��
		auto info = (FullItem<ProcessExitInfo>*)ExAllocatePoolWithTag(PagedPool, sizeof(FullItem<ProcessExitInfo>), DRIVER_TAG);
		if (info == nullptr)
		{
			KdPrint(("when process exiting,failed to allocation\n"));
			return;
		}
		//����ɹ��Ϳ�ʼ�ռ���Ϣ
		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);//��ȡ����ʱ��
		item.Type = ItemType::ProcessExit;//���ò���Ľ�����Ϣ����Ϊö������˳�����
		item.ProcessId = HandleToULong(ProcessId);//�Ѿ��ת��Ϊulong���ͣ���ʵ��һ����
		item.Size = sizeof(ProcessExitInfo);
		PushItem(&info->Entry);//����������ӵ�����β��
	}
}

//�ڹر��ں�ʱ����Ҫ����Ƿ��б���Ľ�����Ϣû���ͷ�
void SysMonUnload(PDRIVER_OBJECT DriverObject)
{
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);//ȡ��ע������¼�

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\sysmon");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);

	//�ͷ�ʣ����¼�����
	while (!IsListEmpty(&g_Globals.ItemHead))
	{
		auto entry = RemoveHeadList(&g_Globals.ItemHead);
		ExFreePool(CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry));
	}

}

//�߳�֪ͨ����
void OnThreadNotiry(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create)
{
	//�����ڴ����洢�߳̽ṹ����Ϣ
	auto size = sizeof(FullItem<ThreadCreateExitInfo>);
	auto info = (FullItem<ThreadCreateExitInfo>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
	if (info == nullptr)
	{
		KdPrint(("failed to allock ThreadCreateExitInfo"));
		return;
	}
	auto& item = info->Data;
	KeQuerySystemTime(&item.Time);
	item.Size = sizeof(item);
	if (Create)
	{
		item.Type = ItemType::ThreadCreate;
	}
	else
	{
		item.Type = ItemType::ThreadExit;
	}
	item.ProcessID = HandleToULong(ProcessId);
	item.ThreadId = HandleToULong(ThreadId);

	PushItem(&info->Entry);
}

//����ģ��֪ͨ����
void PloadImageNotifyRoutine(PUNICODE_STRING FullImageName, HANDLE ProcessId, PIMAGE_INFO ImageInfo)
{
	//�����ڴ�洢ģ����Ϣ
	auto size = sizeof(FullItem<ImageLoadInfo>);
	auto info = (FullItem<ImageLoadInfo>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
	if (info == nullptr)
	{
		KdPrint(("failed to alloc memory for ImageLoadInfo"));
		return;
	}
	auto& item = info->Data;
	KeQuerySystemTime(&item.Time);
	item.ImageSize = ImageInfo->ImageSize;
	item.ProcessId = HandleToULong(ProcessId);
	item.Type = ItemType::ImageLoad;
	item.LoadAddress = ImageInfo->ImageBase;
	if (FullImageName)
	{
		::memcpy(item.ImageFileName, FullImageName->Buffer, min(FullImageName->Length, MaxImageFileSize * sizeof(WCHAR)));
	}
	else
	{
		::wcscpy_s(item.ImageFileName, L"(unknown)");
	}
	//�����Ҫ������ֶ�
	//if (ImageInfo->ExtendedInfoPresent)
	//{
	//	auto exinfo = CONTAINING_RECORD(ImageInfo, IMAGE_INFO_EX, Imageinfo);
	//}

	PushItem(&info->Entry);
}




