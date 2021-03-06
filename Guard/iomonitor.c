#include "ntifs.h"
#include "ntddk.h"
#include "iomonitor.h"
#include "OwnDispatch.h"
#include "FilterDispatch.h"
#include "FastIo.h"
#include "IFunc.h"
#include "log.h"
#include "SMBuffer.h"
#include "LoadDatFile.h"
#include "DatFileEnumator.h"
#include "SMFile.h"
#include "SbScaner.h"
#include "IOCTL.h"
#include "DatFileLoader.h"
#include "StateMachine.h"
#include "JDecryptedFile.h"
#include "hook.h"
#include "FileIoStruct.h"

const UCHAR COUNTFILEJBUFFER = CountBuffer ;

#ifndef DATFILENAME 
	#define DATFILENAME L"\\??\\C:\\i386\\JDatfiel.dat"
	#define VIRDATFILENAME L"\\??\\C:\\i386\\VirDatFile.dat"
#endif 


WCHAR deviceNameBuffer[]  = L"\\Device\\JavIomonitor";
WCHAR deviceLinkBuffer[]  = SymblicName;
UNICODE_STRING            deviceNameUnicodeString , deviceLinkUnicodeString ;

GlobalVariable GV ;

NTSTATUS GlobalVariabeleInit () ;
NTSTATUS GlobalVariabeleUnInit (); 
void AttachtoRawDisk () ;
#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGE, DriverUnload)
  #pragma alloc_text(PAGE, DispatchRoutine)
  #pragma alloc_text(INIT, DriverEntry)
  #pragma alloc_text(PAGE, FsChangeNotify) 
  #pragma alloc_text(PAGE, GlobalVariabeleInit) 
  #pragma alloc_text(PAGE, GlobalVariabeleUnInit) 
#endif


//--------------------------------------------------------------
//نقطه شروع برنامه ميباشد در اين تابع دوايس هاي کار با برنامه کاريردي و همجنين مشخص کردن تابع پاسخ گويي به آر آي پي مشخص ميشود.
//با فراخواني تابعي متغير هاي عومي مقدار اوليه ميگيرد و در انتها با فراخواني تابعي به و ثبت تابعي به عنوان کال بک فاکشن رويداد ها ي مربوط به اضافه شدن و رفتن فايل سيستم درايو را ها به دست ميآيد
//--------------------------------------------------------------
NTSTATUS 
DriverEntry(
    IN PDRIVER_OBJECT DriverObject, 
    IN PUNICODE_STRING RegistryPath 
    )
{
	NTSTATUS                Status;
	int i ;
	PDEVICE_OBJECT          IoDevice;

	RtlInitUnicodeString (&deviceNameUnicodeString,
                          deviceNameBuffer );

	Status = IoCreateDevice (DriverObject ,
					sizeof (DEVICE_EXTENSION_AV),
					&deviceNameUnicodeString , 
					FILE_DEVICE_DISK_FILE_SYSTEM ,
					0 , 
					TRUE ,
					&IoDevice);

	if(!NT_SUCCESS(Status)) 
	{
		return Status;
	}

	memset ( IoDevice->DeviceExtension , 0 , sizeof(DEVICE_EXTENSION_AV) ) ;

	RtlInitUnicodeString ( &deviceLinkUnicodeString,
                           deviceLinkBuffer ); 

	Status = IoCreateSymbolicLink (&deviceLinkUnicodeString,
                                       &deviceNameUnicodeString );
    if(!NT_SUCCESS(Status)) 
	{
		IoDeleteDevice( IoDevice );
		PutLog ( L"Can not create symbolic link " , Status ) ;
		return Status;
	}

	GV.SMAV_Driver = DriverObject ; 


	for( i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++ )
	{
       DriverObject->MajorFunction[i] = DispatchRoutine ;
	   OwnMajorFunction[i] = OwnDefualtRutin ;
	   FilterMajorFunction[i] = FilterDefualtRutin ;
    }

	OwnMajorFunction[IRP_MJ_DEVICE_CONTROL] =  DeviceControlRutin ; 
	OwnMajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = DeviceFsControl ;
	
	FilterMajorFunction [ IRP_MJ_READ ]  = FilterReadDefualtRutin ;
	FilterMajorFunction [ IRP_MJ_CREATE ]  = FilterCreateFileRutin ;
	FilterMajorFunction [ IRP_MJ_FILE_SYSTEM_CONTROL ]  = FilterFSControl ; 
	FilterMajorFunction [IRP_MJ_CLOSE] = FilterCloseFileRutin ;

	DriverObject->FastIoDispatch = &FastIOHook;
	((PDEVICE_EXTENSION_AV)(IoDevice->DeviceExtension))->TYPE = GUIDEVICE ;
	DriverObject->DriverUnload  = DriverUnload;

	Status = GlobalVariabeleInit () ;
	if (!NT_SUCCESS (Status))
	{
		IoDeleteSymbolicLink( & deviceLinkUnicodeString );
		IoDeleteDevice( IoDevice ); 
		PutLog ( L"Can not initialize Gelobal Varibale" , Status ) ;
		return Status ;
	}

	Status = IoRegisterFsRegistrationChange (DriverObject ,FsChangeNotify ) ;
    if(!NT_SUCCESS(Status)) 
	{
		IoDeleteSymbolicLink( & deviceLinkUnicodeString );
		IoDeleteDevice( IoDevice ); 
		PutLog ( L"Can not Register fs Registeration rutin  " , Status ) ;
		return Status;
	}

	
	AttachtoRawDisk();
	/*
	#ifdef IOHOOKMONITOR
	G_ulMonitorProcId = 0;
	if (NT_SUCCESS(InitMonitorLogFile()))
	{
		HookApi();
	}
	#endif
	*/

	IoDevice->Flags |= DO_BUFFERED_IO  ;
	ClearFlag(IoDevice->Flags, DO_DEVICE_INITIALIZING);

	return STATUS_SUCCESS;

}
//-------------------------------------------------------------
VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	NTSTATUS                Status;
	int i ;
	PAGED_CODE();
	DbgPrint ("JavIomonitor : DriverUnload \n") ;
	IoUnregisterFsRegistrationChange ( DriverObject , FsChangeNotify) ;
	Status = IoDeleteSymbolicLink( & deviceLinkUnicodeString );
	if ( !NT_SUCCESS(Status)) 
	{
		DbgPrint ("JavIomonitor : Can not Delete symblic link \n") ;
	}
	IoUnregisterFsRegistrationChange(DriverObject , FsChangeNotify);

	UnHookAllDevice(DriverObject) ;
	GlobalVariabeleUnInit();
}
//-----------------------------------------------------------------------------
NTSTATUS DispatchRoutine (IN PDEVICE_OBJECT DeviceObject,IN PIRP Irp)
{
	PIO_STACK_LOCATION  irpStack ;
	NTSTATUS            Status  = STATUS_NO_SUCH_DEVICE;
	PAGED_CODE();
	
	irpStack = IoGetCurrentIrpStackLocation (Irp);

	if (((PDEVICE_EXTENSION_AV)(DeviceObject->DeviceExtension))->TYPE == FILTERDRIVER )   
	{
		 Status = FilterMajorFunction[irpStack->MajorFunction] ( DeviceObject , Irp , irpStack ) ;
	}
	else if (((PDEVICE_EXTENSION_AV)(DeviceObject->DeviceExtension))->TYPE == GUIDEVICE ) 
	{
		 Status = OwnMajorFunction[irpStack->MajorFunction] ( DeviceObject , Irp , irpStack ) ;
	}
	
	return Status ;
}
//--------------------------------------------------------------------------------
// پس از اتصال به ديواس فايل سيستم پاس شده به تابع کال بک تمامي ديواس هاي متصل به درايو آن برشماري شده و به آنها هم متصل ميشويم 
VOID FsChangeNotify ( IN PDEVICE_OBJECT DeviceObject,IN BOOLEAN FsActive )
{
	NTSTATUS            Status;
	PDEVICE_EXTENSION_AV hookExtension; 

	PAGED_CODE();

	if (FsActive == TRUE)
	{
		if ( IsValidFileSystemForHook(DeviceObject) )
		{ 
			ExAcquireFastMutex (&GV.gbAttachMutex)  ;
			Status = HookByDevice (GV.SMAV_Driver , DeviceObject , FileSystemDevice , NULL) ;	
			ExReleaseFastMutex  (&GV.gbAttachMutex)  ;
			if (NT_SUCCESS(Status))
			{
				AttachToMountVolume(GV.SMAV_Driver , DeviceObject);
			}
		}
	}
	else 
	{
		UnHookByDevice(DeviceObject) ;
	}
}
//----------------------------------------------------------
// ايجاد فضا براي متغيير ها ي از نوع اشاره گر  و مقدار دهي اوليه متغير ها
// در اينجا يک پول از داده ها ايجاد ميگردد تا در زماني که فايل ببرسي ميشود از آن بررسي ميشود
//  پول شامل بافر کار با فايل همچنين منابع منطبق کردن الگو ها که همگي به هم متصل ميشوند توسط تابع 
// InitSbScaner
// InitDatFileParser
NTSTATUS GlobalVariabeleInit()
{
	int i  ;
	NTSTATUS   Status;
	PLIST_ENTRY ListEntry ;
	PFileObjectEntry TempNode ;

	PAGED_CODE();

#ifdef LOGTOFILE
	Status = InitLogFile () ; 
	if ( !NT_SUCCESS (Status) )
	{
		return Status ; 
	}
#endif
    GV.u32DemandConfig = SMAV_SETTING_NOT_SET;

	ExInitializeFastMutex(&GV.gbAttachMutex);
	ExInitializeFastMutex(&GV.gbAllocLookAside);
	
	GV.LoadDatFile =(PtagLoadDatFile) ExAllocatePoolWithTag(NonPagedPool  , sizeof (tagLoadDatFile) , DRIVERTAG) ;
	if (!GV.LoadDatFile)
	{
	
		#ifdef LOGTOFILE
			CloseLogFile();
		#endif
		return STATUS_INSUFFICIENT_RESOURCES ;
	}
	
	if (!NT_SUCCESS (CryptOpen (&GV.LoadDatFile->m_pocFile , DATFILENAME) ))
	{
			#ifdef LOGTOFILE
				CloseLogFile();
			#endif
			ExFreePoolWithTag(GV.LoadDatFile , DRIVERTAG) ;
			return STATUS_INSUFFICIENT_RESOURCES ;
	}
	InitLoadDatFile( GV.LoadDatFile );
	if (LoadSbData(GV.LoadDatFile) == FALSE ) 
	{
			#ifdef LOGTOFILE
				CloseLogFile();
			#endif
			UnLoadDatFile(GV.LoadDatFile) ;
			CryptClose(&GV.LoadDatFile->m_pocFile);
			ExFreePoolWithTag(GV.LoadDatFile , DRIVERTAG) ;
			return STATUS_INSUFFICIENT_RESOURCES ;
	}
/*	
	// Kia Added Start
	//////////////////////////////////////////////////////////////////////////
	GV.pocSMVirusDatFile = (PSMVirusDatFile) ExAllocatePoolWithTag(NonPagedPool  , sizeof (SMVirusDatFile) , DRIVERTAG) ;
	if (!GV.pocSMVirusDatFile)
	{

#ifdef LOGTOFILE
		CloseLogFile();
#endif
		return STATUS_INSUFFICIENT_RESOURCES ;
	}


	GV.pocSMVirusDatFile->m_posFile = (CryptFile* ) ExAllocatePoolWithTag(NonPagedPool  , sizeof (CryptFile) , DRIVERTAG) ;
	if (!GV.pocSMVirusDatFile->m_posFile)
	{
#ifdef LOGTOFILE
		CloseLogFile();
#endif
		ExFreePoolWithTag(GV.pocSMVirusDatFile , DRIVERTAG) ;

		UnLoadDatFile(GV.LoadDatFile) ;
		CryptClose(&GV.LoadDatFile->m_pocFile);
		ExFreePoolWithTag(GV.LoadDatFile , DRIVERTAG) ;

		return STATUS_INSUFFICIENT_RESOURCES ;
	}

 	if (!NT_SUCCESS (CryptOpen (GV.pocSMVirusDatFile->m_posFile , VIRDATFILENAME) ))
 	{
 #ifdef LOGTOFILE
 		CloseLogFile();
 #endif
 		ExFreePoolWithTag(GV.pocSMVirusDatFile->m_posFile , DRIVERTAG) ;
 		ExFreePoolWithTag(GV.pocSMVirusDatFile , DRIVERTAG) ;
 
 		UnLoadDatFile(GV.LoadDatFile) ;
 		CryptClose(&GV.LoadDatFile->m_pocFile);
 		ExFreePoolWithTag(GV.LoadDatFile , DRIVERTAG) ;
 		return STATUS_INSUFFICIENT_RESOURCES ;
 	}

// 	if (!NT_SUCCESS (SMCreateFileForRead (GV.pocSMVirusDatFile->m_posFile , VIRDATFILENAME) ))
// 	{
// #ifdef LOGTOFILE
// 		CloseLogFile();
// #endif
// 		ExFreePoolWithTag(GV.pocSMVirusDatFile->m_posFile , DRIVERTAG) ;
// 		ExFreePoolWithTag(GV.pocSMVirusDatFile , DRIVERTAG) ;
// 
// 		UnLoadDatFile(GV.LoadDatFile) ;
// 		CryptClose(&GV.LoadDatFile->m_pocFile);
// 		ExFreePoolWithTag(GV.LoadDatFile , DRIVERTAG) ;
// 		return STATUS_INSUFFICIENT_RESOURCES ;
// 	}

	if(LoadVMDatFile())
	{
#ifdef LOGTOFILE
		CloseLogFile();
#endif
		ExFreePoolWithTag(GV.pocSMVirusDatFile->m_posFile , DRIVERTAG) ;
		ExFreePoolWithTag(GV.pocSMVirusDatFile , DRIVERTAG) ;

		UnLoadDatFile(GV.LoadDatFile) ;
		CryptClose(GV.pocSMVirusDatFile->m_posFile);
		CryptClose(&GV.LoadDatFile->m_pocFile);
		ExFreePoolWithTag(GV.LoadDatFile , DRIVERTAG) ;
		return STATUS_INSUFFICIENT_RESOURCES ;
	}
*/
	//////////////////////////////////////////////////////////////////////////
	// End

	for ( i = 0 ; i < COUNTFILEJBUFFER ; i++  )
	{
		GV.DatFileParserPool[i] = (PDatFileParser) ExAllocatePoolWithTag(NonPagedPool  , sizeof (DatFileParser) , DRIVERTAG) ;
		if (GV.DatFileParserPool[i] == NULL)
		{
			while (i >= 0)
			{
				ExFreePoolWithTag(GV.DatFileParserPool[i] , DRIVERTAG);
				i--;
			}
			#ifdef LOGTOFILE
				CloseLogFile();
			#endif
			UnLoadDatFile(GV.LoadDatFile) ;
			CryptClose(&GV.LoadDatFile->m_pocFile);
			CryptClose(GV.pocSMVirusDatFile->m_posFile);
			ExFreePoolWithTag(GV.LoadDatFile , DRIVERTAG) ;
			return STATUS_INSUFFICIENT_RESOURCES ;
		}
		InitDatFileParser(GV.DatFileParserPool[i], i , GV.LoadDatFile) ;
	}
	
	for ( i = 0 ; i < COUNTFILEJBUFFER ; i++  )
	{
		Status = SMBufferInit (&GV.BufferPool[i] , 2 , 4096);
		if ( !NT_SUCCESS (Status) )
		{
			while (i >= 0)
			{
				SMBufferUninitialize(&GV.BufferPool[i]);
				i--;
			}
			for ( i = 0 ; i < COUNTFILEJBUFFER ; i++  )
			{
				ExFreePoolWithTag(GV.DatFileParserPool[i] , DRIVERTAG);
			}
			#ifdef LOGTOFILE
				CloseLogFile();
			#endif
			UnLoadDatFile(GV.LoadDatFile) ;
			CryptClose(&GV.LoadDatFile->m_pocFile);
			CryptClose(GV.pocSMVirusDatFile->m_posFile);
			ExFreePoolWithTag(GV.LoadDatFile , DRIVERTAG) ;
			return STATUS_INSUFFICIENT_RESOURCES ;
		}
	}
	/*
	// Kia Added Start 
	//////////////////////////////////////////////////////////////////////////

	for ( i = 0 ; i < COUNTFILEJBUFFER ; i++  )
	{
		GV.parosVMStack[i] = (PSMVirtualMachineStack)SMAlloc(sizeof(SMVirtualMachineStack));
		if (GV.parosVMStack[i])
		{
			Status = InitializeVirtualMachineStack(GV.parosVMStack[i]);
		}		
		if ( !NT_SUCCESS (Status) || !GV.parosVMStack[i] )
		{
			i--;
			while (i >= 0)
			{
				UnInitializeVirtualMachineStack(GV.parosVMStack[i]);
				SMFree(GV.parosVMStack[i]);
				i--;
			}
#ifdef LOGTOFILE
			CloseLogFile();
#endif
			UnLoadDatFile(GV.LoadDatFile) ;
			CryptClose(&GV.LoadDatFile->m_pocFile);
			CryptClose(GV.pocSMVirusDatFile->m_posFile);
			ExFreePoolWithTag(GV.LoadDatFile , DRIVERTAG) ;
			return STATUS_INSUFFICIENT_RESOURCES ;
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// End 
	*/
	for ( i = 0 ; i < COUNTFILEJBUFFER ; i++  )
	{
		GV.SbScanerPool[i] = (PSbScaner) ExAllocatePoolWithTag(NonPagedPool  , sizeof (SbScaner) , DRIVERTAG) ;
		if (GV.SbScanerPool[i] == NULL)
		{
			while (i >= 0)
			{
				ExFreePoolWithTag(GV.SbScanerPool[i] , DRIVERTAG);
				i--;
			}
			for ( i = 0 ; i < COUNTFILEJBUFFER ; i++  )
			{
				SMBufferUninitialize(&GV.BufferPool[i]) ;
				ExFreePoolWithTag(GV.DatFileParserPool[i] , DRIVERTAG);
			}
			#ifdef LOGTOFILE
				CloseLogFile();
			#endif
			UnLoadDatFile(GV.LoadDatFile) ;
			CryptClose(&GV.LoadDatFile->m_pocFile);
			CryptClose(GV.pocSMVirusDatFile->m_posFile);
			ExFreePoolWithTag(GV.LoadDatFile , DRIVERTAG) ;
			return STATUS_INSUFFICIENT_RESOURCES ;
		}
		InitSbScaner(GV.SbScanerPool[i] , GV.DatFileParserPool[i] , GV.BufferPool[i], GV.parosVMStack[i]);
	}
	
	GV.StartBufferAccsess = 0;
	KeInitializeSemaphore(&GV.BufferSemaphore , COUNTFILEJBUFFER  , COUNTFILEJBUFFER  );
	ExInitializeFastMutex (&GV.BufferPointerLock);

	GV.GuiConnect = FALSE ;
	return STATUS_SUCCESS ;
}
//---------------------------------------------------------------------------
NTSTATUS GlobalVariabeleUnInit ()
{
	PLIST_ENTRY ListEntry ;
	KIRQL CurrentIRQL ; 
	PFileObjectEntry TempNode ;
	ULONG index ;
	PAGED_CODE();
	#ifdef LOGTOFILE
		CloseLogFile();
	#endif		
	for ( index = 0 ; index < COUNTFILEJBUFFER ; index++  )
	{
		ExFreePoolWithTag(GV.DatFileParserPool[index] , DRIVERTAG);
		ExFreePoolWithTag(GV.SbScanerPool[index] , DRIVERTAG);
		SMBufferUninitialize(&GV.BufferPool[index]) ;
	}
	UnLoadDatFile(GV.LoadDatFile) ;
	CryptClose(&GV.LoadDatFile->m_pocFile);
	ExFreePoolWithTag(GV.LoadDatFile , DRIVERTAG) ; 
	
	UnLoadVMDatFile();
	CryptClose(GV.pocSMVirusDatFile->m_posFile);
	ExFreePoolWithTag(GV.pocSMVirusDatFile->m_posFile, DRIVERTAG) ; 
	ExFreePoolWithTag(GV.pocSMVirusDatFile, DRIVERTAG) ; 
	
	return STATUS_SUCCESS ;
}
//---------------------------------------------------------------------------
//به دليل اينکه کال بک فاکشن ريجستر شده به اعضاي رو ديسک ها فراخواني نميشون و نمتوان آنها رابدست آورد ميبايد به اين شکل آنها ها را هم هوک کرد
void AttachtoRawDisk()
{

	UNICODE_STRING nameString;
	PDEVICE_OBJECT rawDeviceObject;
	PFILE_OBJECT   fileObject;
	NTSTATUS       Status;

	RtlInitUnicodeString( &nameString, L"\\Device\\RawDisk" );

	Status = IoGetDeviceObjectPointer(&nameString,FILE_READ_ATTRIBUTES, &fileObject, &rawDeviceObject );

	if (NT_SUCCESS( Status ))
	{
		FsChangeNotify( rawDeviceObject, TRUE );
		ObDereferenceObject( fileObject );
	}

	RtlInitUnicodeString( &nameString, L"\\Device\\RawCdRom" ); 

	Status = IoGetDeviceObjectPointer(&nameString, FILE_READ_ATTRIBUTES , &fileObject, &rawDeviceObject );

	if (NT_SUCCESS( Status ))
	{
		FsChangeNotify( rawDeviceObject, TRUE );
		ObDereferenceObject( fileObject );
	}

}
//---------------------------------------------------------------------------