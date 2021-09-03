#pragma once
//��װ��һ���Զ��Ļ�����
template<typename TLock>
struct AutoLock {
	AutoLock(TLock& lock):_lock(lock){
		_lock.Lock();
	}
	~AutoLock()
	{
		_lock.Unlock();
	}

private:
	TLock& _lock;
};