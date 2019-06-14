#include "acl_stdafx.hpp"
#ifndef ACL_PREPARE_COMPILE
#include "acl_cpp/stdlib/log.hpp"
#include "acl_cpp/stream/socket_stream.hpp"
#include "acl_cpp/master/master_threads.hpp"
#endif

namespace acl
{

static master_threads* __mt = NULL;

master_threads::master_threads(void)
{
	// ȫ�־�̬����
	acl_assert(__mt == NULL);
	__mt = this;
}

master_threads::~master_threads(void)
{
}

//////////////////////////////////////////////////////////////////////////

static bool has_called = false;

void master_threads::run_daemon(int argc, char** argv)
{
#ifndef ACL_WINDOWS
	// ÿ������ֻ����һ��ʵ��������
	acl_assert(has_called == false);
	has_called = true;
	daemon_mode_ = true;

	// ���� acl ��������ܵĶ��߳�ģ��
	acl_threads_server_main(argc, argv, service_main, NULL,
		ACL_MASTER_SERVER_ON_ACCEPT, service_on_accept,
		ACL_MASTER_SERVER_ON_HANDSHAKE, service_on_handshake,
		ACL_MASTER_SERVER_ON_TIMEOUT, service_on_timeout,
		ACL_MASTER_SERVER_ON_CLOSE, service_on_close,
		ACL_MASTER_SERVER_PRE_INIT, service_pre_jail,
		ACL_MASTER_SERVER_POST_INIT, service_init,
		ACL_MASTER_SERVER_EXIT_TIMER, service_exit_timer,
		ACL_MASTER_SERVER_EXIT, service_exit,
		ACL_MASTER_SERVER_THREAD_INIT, thread_init,
		ACL_MASTER_SERVER_THREAD_EXIT, thread_exit,
		ACL_MASTER_SERVER_BOOL_TABLE, conf_.get_bool_cfg(),
		ACL_MASTER_SERVER_INT64_TABLE, conf_.get_int64_cfg(),
		ACL_MASTER_SERVER_INT_TABLE, conf_.get_int_cfg(),
		ACL_MASTER_SERVER_STR_TABLE, conf_.get_str_cfg(),
		ACL_MASTER_SERVER_END);
#else
	logger_fatal("no support win32 yet!");
#endif
}

bool master_threads::run_alone(const char* addrs, const char* path /* = NULL */,
	unsigned int, int)
{
	// ÿ������ֻ����һ��ʵ��������
	acl_assert(has_called == false);
	has_called = true;
	daemon_mode_ = false;
	acl_assert(addrs && *addrs);

	int  argc = 0;
	const char *argv[9];

	const char* proc = acl_process_path();
	argv[argc++] = proc ? proc : "demo";
	argv[argc++] = "-L";
	argv[argc++] = addrs;
	if (path && *path)
	{
		argv[argc++] = "-f";
		argv[argc++] = path;
	}

	// ���� acl ��������ܵĶ��߳�ģ��
	acl_threads_server_main(argc, (char**) argv, service_main, NULL,
		ACL_MASTER_SERVER_ON_ACCEPT, service_on_accept,
		ACL_MASTER_SERVER_ON_HANDSHAKE, service_on_handshake,
		ACL_MASTER_SERVER_ON_TIMEOUT, service_on_timeout,
		ACL_MASTER_SERVER_ON_CLOSE, service_on_close,
		ACL_MASTER_SERVER_PRE_INIT, service_pre_jail,
		ACL_MASTER_SERVER_POST_INIT, service_init,
		ACL_MASTER_SERVER_EXIT_TIMER, service_exit_timer,
		ACL_MASTER_SERVER_EXIT, service_exit,
		ACL_MASTER_SERVER_THREAD_INIT, thread_init,
		ACL_MASTER_SERVER_THREAD_EXIT, thread_exit,
		ACL_MASTER_SERVER_BOOL_TABLE, conf_.get_bool_cfg(),
		ACL_MASTER_SERVER_INT64_TABLE, conf_.get_int64_cfg(),
		ACL_MASTER_SERVER_INT_TABLE, conf_.get_int_cfg(),
		ACL_MASTER_SERVER_STR_TABLE, conf_.get_str_cfg(),
		ACL_MASTER_SERVER_END);

	return true;
}

//////////////////////////////////////////////////////////////////////////

void master_threads::thread_disable_read(socket_stream* stream)
{
	ACL_EVENT* event = get_event();

	if (event == NULL)
		logger_error("event NULL");
	else
		acl_event_disable_readwrite(event, stream->get_vstream());
}

void master_threads::thread_enable_read(socket_stream* stream)
{
	ACL_EVENT* event = get_event();
	if (event == NULL)
	{
		logger_error("event NULL");
		return;
	}

	acl_pthread_pool_t* threads = acl_threads_server_threads();
	if (threads != NULL)
		acl_threads_server_enable_read(event, threads,
			stream->get_vstream());
	else
		logger_error("threads NULL!");
}

//////////////////////////////////////////////////////////////////////////

void master_threads::service_pre_jail(void*)
{
	acl_assert(__mt != NULL);

	ACL_EVENT* eventp = acl_threads_server_event();
	__mt->set_event(eventp);

	__mt->proc_pre_jail();
}

void master_threads::service_init(void*)
{
	acl_assert(__mt != NULL);

	__mt->proc_inited_ = true;
	__mt->proc_on_init();
}

int master_threads::service_exit_timer(size_t nclients, size_t nthreads)
{
	acl_assert(__mt != NULL);
	return __mt->proc_exit_timer(nclients, nthreads) == true ? 1 : 0;
}

void master_threads::service_exit(void*)
{
	acl_assert(__mt != NULL);
	__mt->proc_on_exit();
}

int master_threads::thread_init(void*)
{
	acl_assert(__mt != NULL);
	__mt->thread_on_init();
	return 0;
}

void master_threads::thread_exit(void*)
{
	acl_assert(__mt != NULL);
	__mt->thread_on_exit();
}

int master_threads::service_on_accept(ACL_VSTREAM* client)
{
	// client->context ��Ӧ��ռ��
	if (client->context != NULL)
		logger_fatal("client->context not null!");

	socket_stream* stream = NEW socket_stream();

	// ���� client->context Ϊ�����󣬸�������ͳһ��
	// service_on_close �б��ͷ�
	client->context = stream;

	if (stream->open(client) == false)
	{
		logger_error("open stream error(%s)", acl_last_serror());
		// ���� -1 ���ϲ��ܵ��� service_on_close ���̣�������
		// �ͷ� stream ����
		return -1;
	}

	acl_assert(__mt != NULL);

	// �������� thread_on_accept �������� false����ֱ�ӷ��ظ��ϲ�
	// ��� -1�����ϲ����ٵ��� service_on_close ���̣��Ӷ��ڸù���
	// �н� stream �����ͷ�
	if (__mt->thread_on_accept(stream) == false)
		return -1;

	// �������� thread_on_handshake �������� false����ֱ�ӷ��ظ��ϲ�
	// ��� -1�����ϲ����ٵ��� service_on_close ���̣��Ӷ��ڸù���
	// �н� stream �����ͷ�
	if (__mt->thread_on_handshake(stream) == false)
		return -1;

	// ���� 0 ��ʾ���Լ�������ÿͻ������ӣ��Ӷ����ϲ��ܽ�������
	// ����ؼ�����
	return 0;
}

int master_threads::service_on_handshake(ACL_VSTREAM *client)
{
	acl_assert(__mt != NULL);

	// client->context �� service_on_accept �б�����
	socket_stream* stream = (socket_stream*) client->context;
	if (stream == NULL)
		logger_fatal("client->context is null!");

	if (__mt->thread_on_handshake(stream) == true)
		return 0;
	return -1;
}

int master_threads::service_main(ACL_VSTREAM *client, void*)
{
	acl_assert(__mt != NULL);

	// client->context �� service_on_accept �б�����
	socket_stream* stream = (socket_stream*) client->context;
	if (stream == NULL)
		logger_fatal("client->context is null!");

	// ����������麯��ʵ�֣�������� true ��ʾ�ÿ�ܼ�����ظ���������
	// ������Ҫ�رո���
	// ���ϲ��ܷ���ֵ�������£�
	// 0 ��ʾ��������ӱ��ֳ����ӣ�����Ҫ������ظ������Ƿ�ɶ�
	// -1 ��ʾ��Ҫ�رո�����
	// 1 ��ʾ���ټ�ظ�����

	if (__mt->thread_on_read(stream) == true)
	{
		// ��������ڷ��� true ��ϣ����ܼ������������ֱ�ӷ��ظ���� 1
		if (!__mt->keep_read(stream))
			return 1;

		// ������Ҫ�������Ƿ��Ѿ��رգ�����رգ�����뷵�� -1
		if (stream->eof())
		{
			logger_error("DISCONNECTED, CLOSING, FD: %d",
				(int) stream->sock_handle());
			return -1;
		}

		// ���� 0 ��ʾ������ظ����Ŀɶ�״̬
		return 0;
	}

	// ���� -1 ��ʾ���ϲ��������ر������ϲ����������ر���ǰ
	// ����ص� service_on_close ���̽������ر�ǰ���ƺ�������
	// stream ������ service_on_close �б��ͷ�
	return -1;
}

int master_threads::service_on_timeout(ACL_VSTREAM* client, void*)
{
	socket_stream* stream = (socket_stream*) client->context;
	if (stream == NULL)
		logger_fatal("client->context is null!");

	acl_assert(__mt != NULL);

	return __mt->thread_on_timeout(stream) == true ? 0 : -1;
}

void master_threads::service_on_close(ACL_VSTREAM* client, void*)
{
	socket_stream* stream = (socket_stream*) client->context;
	if (stream == NULL)
		logger_fatal("client->context is null!");

	acl_assert(__mt != NULL);

	// �������ຯ���Խ�Ҫ�رյ��������ƺ���
	__mt->thread_on_close(stream);

	// �������������İ󶨹�ϵ���������Է�ֹ��ɾ��������ʱ
	// �����ر�������������Ϊ����������Ҫ�ڱ��������غ���
	// ����Զ��ر�
	(void) stream->unbind();
	delete stream;
}

}  // namespace acl
