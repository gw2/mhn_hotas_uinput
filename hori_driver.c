/*
 * hori-driver.exe: gives the Flightstick 2 (USB 06D3:0F10) Windows' built-in
 * WinUSB driver, the way Zadig does, but without any UI or device picking.
 *
 *   hori-driver install     write the driver package, self-sign it, install it
 *   hori-driver uninstall   remove the package, its certificate and the binding
 *   hori-driver status      say whether the package is installed
 *
 * The package is only an INF that points the stick at the WinUSB driver that
 * ships with Windows 10/11 (no co-installers, no driver binaries of our own),
 * plus a catalog. Windows wants the catalog signed, so, like Zadig, this signs
 * it with a certificate made for this one package, trusts that certificate,
 * and deletes its private key so it can't sign anything else. The catalog and
 * signing code is libwdi's pki.c (LGPLv3, by Pete Batard; third_party/libwdi).
 *
 * Runs as admin (hori_driver.manifest). Exit code 0 on success.
 */
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "libwdi.h"
#include "stdfn.h"

/* From libwdi's pki.c */
BOOL CreateCat(LPCSTR szCatPath, LPCSTR szHWID, LPCSTR szSearchDir, LPCSTR* szFileList, DWORD cFileList);
BOOL SelfSignFile(LPCSTR szFileName, LPCSTR szCertSubject);
BOOL RemoveCertFromStore(LPCSTR szCertSubject, LPCSTR szStoreName);

#define HW_ID           "USB\\VID_06D3&PID_0F10"
#define INF_NAME        "hori-flightstick2.inf"
#define CAT_NAME        "hori-flightstick2.cat"
#define CERT_SUBJECT    "CN=Flightstick 2 WinUSB package for USB\\VID_06D3&PID_0F10 (hori-driver)"
#define REG_KEY         "SOFTWARE\\Flightstick2"
#define REG_VALUE       "DriverInf"

static const char inf_text[] =
	"; " INF_NAME ": gives the HORI/Namco Flightstick 2 (USB 06D3:0F10) Windows' built-in WinUSB driver.\r\n"
	"; Written by hori-driver.exe, https://github.com/gw2/mhn_hotas_uinput\r\n"
	"\r\n"
	"[Version]\r\n"
	"Signature   = \"$Windows NT$\"\r\n"
	"Class       = USBDevice\r\n"
	"ClassGuid   = {88bae032-5a81-49f0-bc3d-a4ff138216d6}\r\n"
	"Provider    = %Provider%\r\n"
	"CatalogFile = " CAT_NAME "\r\n"
	"DriverVer   = 10/31/2026,1.0.0.0\r\n"
	"PnpLockdown = 1\r\n"
	"\r\n"
	"[Manufacturer]\r\n"
	"%Provider% = Stick,NTamd64,NTarm64,NTx86\r\n"
	"\r\n"
	"[Stick.NTamd64]\r\n"
	"%DeviceName% = USB_Install, " HW_ID "\r\n"
	"\r\n"
	"[Stick.NTarm64]\r\n"
	"%DeviceName% = USB_Install, " HW_ID "\r\n"
	"\r\n"
	"[Stick.NTx86]\r\n"
	"%DeviceName% = USB_Install, " HW_ID "\r\n"
	"\r\n"
	"[USB_Install]\r\n"
	"Include = winusb.inf\r\n"
	"Needs   = WINUSB.NT\r\n"
	"\r\n"
	"[USB_Install.Services]\r\n"
	"Include = winusb.inf\r\n"
	"Needs   = WINUSB.NT.Services\r\n"
	"\r\n"
	"[USB_Install.HW]\r\n"
	"AddReg = Dev_AddReg\r\n"
	"\r\n"
	"[Dev_AddReg]\r\n"
	"HKR,,DeviceInterfaceGUIDs,0x10000,\"{6c1d4a3e-8f2b-4e7a-9c5d-0f3e2a1b7c64}\"\r\n"
	"\r\n"
	"[Strings]\r\n"
	"Provider   = \"Flightstick 2 driver\"\r\n"
	"DeviceName = \"HORI Flightstick 2 (WinUSB)\"\r\n";

/* ---- what pki.c needs from the rest of libwdi ---- */

/* Only Windows 10/11 are supported, which is all pki.c's version check needs to know. */
int nWindowsVersion = WINDOWS_10;

void GetWindowsVersion(void)
{
	nWindowsVersion = WINDOWS_10;
}

void wdi_log(enum wdi_log_level level, const char *function, const char *format, ...)
{
	static const char *const names[] = { "debug", "info", "warning", "error", "" };
	va_list ap;
	if( level < WDI_LOG_LEVEL_INFO )
		return;
	printf("  [%s] ", names[level]);
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	printf("\n");
}

char *wdi_windows_error_str(uint32_t retval)
{
	static char buf[300];
	DWORD code = retval ? retval : GetLastError();
	if( !FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, buf, sizeof(buf), NULL) )
		snprintf(buf, sizeof(buf), "error 0x%08lx", (unsigned long)code);
	return buf;
}

/* ---- helpers ---- */

static int package_dir(char *out, size_t size)
{
	char base[MAX_PATH];
	if( S_OK != SHGetFolderPathA(NULL, CSIDL_COMMON_APPDATA, NULL, 0, base) )
		return 0;
	snprintf(out, size, "%s\\Flightstick2", base);
	CreateDirectoryA(out, NULL);
	snprintf(out, size, "%s\\Flightstick2\\driver", base);
	CreateDirectoryA(out, NULL);
	return 1;
}

static void reg_set(const char *oem)
{
	HKEY k;
	if( ERROR_SUCCESS == RegCreateKeyExA(HKEY_LOCAL_MACHINE, REG_KEY, 0, NULL, 0, KEY_WRITE | KEY_WOW64_64KEY, NULL, &k, NULL) ) {
		RegSetValueExA(k, REG_VALUE, 0, REG_SZ, (const BYTE *)oem, (DWORD)strlen(oem) + 1);
		RegCloseKey(k);
	}
}

static int reg_get(char *out, DWORD size)
{
	HKEY k;
	int ok = 0;
	if( ERROR_SUCCESS == RegOpenKeyExA(HKEY_LOCAL_MACHINE, REG_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &k) ) {
		DWORD type, len = size;
		ok = ERROR_SUCCESS == RegQueryValueExA(k, REG_VALUE, NULL, &type, (BYTE *)out, &len) && REG_SZ == type;
		RegCloseKey(k);
	}
	return ok;
}

static void reg_clear(void)
{
	HKEY k;
	if( ERROR_SUCCESS == RegOpenKeyExA(HKEY_LOCAL_MACHINE, REG_KEY, 0, KEY_WRITE | KEY_WOW64_64KEY, &k) ) {
		RegDeleteValueA(k, REG_VALUE);
		RegCloseKey(k);
	}
}

/* Uninstall the stick's device node(s) so Windows re-detects them with whatever driver is left. */
static int remove_device_nodes(void)
{
	// Not in MinGW's import library for newdev, so look it up.
	typedef BOOL (WINAPI *uninstall_fn)(HWND, HDEVINFO, PSP_DEVINFO_DATA, DWORD, PBOOL);
	HMODULE newdev = LoadLibraryA("newdev.dll");
	uninstall_fn pDiUninstallDevice = newdev ? (uninstall_fn)(void (*)(void))GetProcAddress(newdev, "DiUninstallDevice") : NULL;
	if( !pDiUninstallDevice )
		return 0;

	int n = 0;
	HDEVINFO set = SetupDiGetClassDevsA(NULL, "USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if( INVALID_HANDLE_VALUE == set )
		return 0;
	SP_DEVINFO_DATA dev = { sizeof(dev) };
	for( DWORD i=0; SetupDiEnumDeviceInfo(set, i, &dev); i++ ) {
		char ids[1024];
		if( !SetupDiGetDeviceRegistryPropertyA(set, &dev, SPDRP_HARDWAREID, NULL, (BYTE *)ids, sizeof(ids), NULL) )
			continue;
		for( char *id = ids; *id; id += strlen(id) + 1 ) {
			if( 0 == _stricmp(id, HW_ID) ) {
				BOOL reboot = FALSE;
				if( pDiUninstallDevice(NULL, set, &dev, 0, &reboot) )
					n++;
				break;
			}
		}
	}
	SetupDiDestroyDeviceInfoList(set);
	return n;
}

/* ---- commands ---- */

static int cmd_install(void)
{
	char dir[MAX_PATH], inf[MAX_PATH], cat[MAX_PATH], oem[MAX_PATH];
	LPCSTR files[] = { INF_NAME };

	if( !package_dir(dir, sizeof(dir)) ) {
		printf("Couldn't create the driver package folder.\n");
		return 2;
	}
	snprintf(inf, sizeof(inf), "%s\\%s", dir, INF_NAME);
	snprintf(cat, sizeof(cat), "%s\\%s", dir, CAT_NAME);

	FILE *f = fopen(inf, "wb");
	if( !f ) {
		printf("Couldn't write %s\n", inf);
		return 2;
	}
	fwrite(inf_text, 1, sizeof(inf_text) - 1, f);
	fclose(f);
	printf("Wrote %s\n", inf);

	DeleteFileA(cat);
	if( !CreateCat(cat, HW_ID, dir, files, 1) ) {
		printf("Couldn't create the driver catalog.\n");
		return 3;
	}
	if( !SelfSignFile(cat, CERT_SUBJECT) ) {
		printf("Couldn't sign the driver catalog.\n");
		return 3;
	}
	printf("Created and signed %s\n", cat);

	// Add it to the driver store, so it's used whenever the stick is plugged in.
	if( !SetupCopyOEMInfA(inf, NULL, SPOST_PATH, 0, oem, sizeof(oem), NULL, NULL) ) {
		printf("Couldn't add the driver package to Windows: %s\n", wdi_windows_error_str(0));
		return 4;
	}
	reg_set(oem);
	printf("Added to the driver store as %s\n", oem);

	// And use it now if the stick is plugged in (replacing any driver it has).
	BOOL reboot = FALSE;
	if( UpdateDriverForPlugAndPlayDevicesA(NULL, HW_ID, inf, INSTALLFLAG_FORCE, &reboot) )
		printf("The stick now uses WinUSB.%s\n", reboot ? " Windows asks for a restart to finish." : "");
	else if( ERROR_NO_SUCH_DEVINST == GetLastError() )
		printf("The stick isn't plugged in; it will use WinUSB when it is.\n");
	else {
		printf("Couldn't switch the stick to the driver: %s\n", wdi_windows_error_str(0));
		return 5;
	}
	return 0;
}

static int cmd_uninstall(void)
{
	char oem[MAX_PATH], dir[MAX_PATH], path[MAX_PATH];

	if( reg_get(oem, sizeof(oem)) ) {
		char *name = strrchr(oem, '\\');
		name = name ? name + 1 : oem;
		if( SetupUninstallOEMInfA(name, SUOI_FORCEDELETE, NULL) )
			printf("Removed %s from the driver store.\n", name);
		else
			printf("Couldn't remove %s from the driver store: %s\n", name, wdi_windows_error_str(0));
		reg_clear();
	} else
		printf("No driver package recorded; nothing to remove from the driver store.\n");

	int n = remove_device_nodes();
	if( n )
		printf("Reset the stick's driver (%d device%s); Windows will re-detect it.\n", n, 1 == n ? "" : "s");

	RemoveCertFromStore(CERT_SUBJECT, "Root");
	RemoveCertFromStore(CERT_SUBJECT, "TrustedPublisher");
	printf("Removed the package's certificate.\n");

	if( package_dir(dir, sizeof(dir)) ) {
		snprintf(path, sizeof(path), "%s\\%s", dir, INF_NAME);
		DeleteFileA(path);
		snprintf(path, sizeof(path), "%s\\%s", dir, CAT_NAME);
		DeleteFileA(path);
		RemoveDirectoryA(dir);
	}
	return 0;
}

static int cmd_status(void)
{
	char oem[MAX_PATH];
	if( reg_get(oem, sizeof(oem)) ) {
		printf("Driver package installed: %s\n", oem);
		return 0;
	}
	printf("Driver package not installed.\n");
	return 1;
}

int main(int argc, char **argv)
{
	if( argc == 2 && 0 == strcmp(argv[1], "install") )
		return cmd_install();
	if( argc == 2 && 0 == strcmp(argv[1], "uninstall") )
		return cmd_uninstall();
	if( argc == 2 && 0 == strcmp(argv[1], "status") )
		return cmd_status();

	printf("Usage: hori-driver install | uninstall | status\n"
		"Gives the HORI/Namco Flightstick 2 (USB 06D3:0F10) Windows' built-in WinUSB driver.\n");
	return 1;
}
