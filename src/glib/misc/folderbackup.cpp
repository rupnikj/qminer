#include "folderbackup.h"

//
// TBackupLogInfo
//
TBackupLogInfo::TBackupLogInfo(const PJsonVal& Json)
{
	FolderName = Json->GetObjStr("folderName");
	SecsNeeded = Json->GetObjInt("secsNeeded");
	LogInfo = Json->GetObjStr("logInfo");
}

TBackupLogInfo::TBackupLogInfo(const TStr& _FolderName, const TFlt _SecsNeeded, const TStr& _LogInfo):
	FolderName(_FolderName), SecsNeeded(_SecsNeeded), LogInfo(_LogInfo)
{
}

PJsonVal TBackupLogInfo::GetJson() const
{
	PJsonVal RetJson = TJsonVal::NewObj("folderName", FolderName);
	RetJson->AddToObj("secsNeeded", SecsNeeded);
	RetJson->AddToObj("logInfo", LogInfo);
	return RetJson;
}

//
// TBackupProfile
// 
TBackupProfile::TBackupProfile(const PJsonVal& SettingsJson)
{
	Destination = SettingsJson->GetObjStr("destination");
	if (Destination.Len() > 0 && (Destination.LastCh() != '\\' || Destination.LastCh() != '/'))
		Destination += "/";
	VersionsToKeep = SettingsJson->GetObjInt("versionsToKeep", 1);
	PJsonVal FoldersJson = SettingsJson->GetObjKey("folders");
	EAssertR(FoldersJson->IsArr(), "Expected to get an array of folders");
	for (int N = 0; N < FoldersJson->GetArrVals(); N++) {
		PJsonVal FolderJson = FoldersJson->GetArrVal(N);
		TBackupFolderInfo FolderInfo;
		FolderInfo.Folder = FolderJson->GetObjStr("folder");
		if (FolderJson->IsObjKey("extensions"))
			FolderJson->GetObjStrV("extensions", FolderInfo.Extensions);
		if (FolderInfo.Extensions.IsIn("*"))
			FolderInfo.Extensions.Clr();
		FolderInfo.IncludeSubfolders = FolderJson->GetObjBool("includeSubfolders");
		if (FolderJson->IsObjKey("skipIfContaining"))
			FolderJson->GetObjStrV("skipIfContaining", FolderInfo.SkipIfContainingV);
		FolderV.Add(FolderInfo);
	}
}

TStr TBackupProfile::GetFolderNameForCurrentTime() const
{
	TTm Now = TTm::GetCurLocTm();
	return Now.GetYMDDashStr() + " " + TStr::Fmt("%02d-%02d-%02d", Now.GetHour(), Now.GetMin(), Now.GetSec());
}

// create the actual backup of a given profile
TBackupLogInfo TBackupProfile::CreateBackup()
{
	try {
		// create base destination folder if necessary
		if (!TDir::Exists(Destination))
			TDir::GenDir(Destination);
		// create folder for the current backup
		const TStr DateFolderName = GetFolderNameForCurrentTime();
		const TStr FullDateFolder = Destination + DateFolderName + "/";
		if (!TDir::Exists(FullDateFolder))
			TDir::GenDir(FullDateFolder);

		TTmStopWatch StopWatch(true);

		TStr ErrMsgs;
		for (int N = 0; N < FolderV.Len(); N++) {
			TStr ErrMsg;
			// copy files
			CopyFolder(FullDateFolder, FolderV[N].Folder, FolderV[N].Extensions, FolderV[N].SkipIfContainingV, FolderV[N].IncludeSubfolders, ErrMsg);
			if (ErrMsg != "")
				ErrMsgs += (ErrMsgs.Len() > 0) ? "\n" + ErrMsg : ErrMsg;
		}
		const double Sec = StopWatch.GetSec();
		TStr OutText = ErrMsgs.Len() > 0 ? ErrMsgs : "Backup finished successfully.";
		return TBackupLogInfo(DateFolderName, Sec, OutText);
	}
	catch (PExcept E) {
		return TBackupLogInfo("", 0, E->GetMsgStr());
	}
	catch (...) {
		return TBackupLogInfo("", 0, "Unrecognized exception occured.");
	}
}

// copy files for a particular folder info
void TBackupProfile::CopyFolder(const TStr& BaseTargetFolder, const TStr& SourceFolder, const TStrV& Extensions, const TStrV& SkipIfContainingV, const bool& IncludeSubfolders, TStr& ErrMsg)
{
	try {
		// get the name of the source folder
		TStrV PathV; TFile::SplitPath(SourceFolder, PathV);
		EAssert(PathV.Len() > 0);

		// create the folder in the base target folder
		TStr TargetFolder = BaseTargetFolder + PathV[PathV.Len() - 1] + "/";
		if (!TDir::Exists(TargetFolder))
			TDir::GenDir(TargetFolder);

		// find files to be copied
		TStrV FileV;
		TFFile::GetFNmV(SourceFolder, Extensions, false, FileV);

		TStrV FolderV;
		// copy them
		for (int N = 0; N < FileV.Len(); N++) {
			// we found a file
			if (TFile::Exists(FileV[N])) {
				const TStr FileName = TFile::GetFileName(FileV[N]);
				// is this a file that we wish to ignore?
				bool ShouldCopy = true;
				for (int S = 0; S < SkipIfContainingV.Len(); S++) {
					if (FileName.SearchStr(SkipIfContainingV[S]) >= 0)
						ShouldCopy = false;
				}
				if (!ShouldCopy)
					continue;
				const TStr TargetFNm = TargetFolder + FileName;
				TFile::Copy(FileV[N], TargetFNm);
			}
			// we found a folder
			else {
				FolderV.Add(FileV[N]);
			}
		}

		if (IncludeSubfolders) {
			for (int N = 0; N < FolderV.Len(); N++)
				CopyFolder(TargetFolder, FolderV[N], Extensions, SkipIfContainingV, IncludeSubfolders, ErrMsg);
		}
	}
	catch (PExcept E) {
		if (ErrMsg != "")
			ErrMsg += "\n";
		ErrMsg += "Exception while copying from " + SourceFolder + ": " + E->GetMsgStr();
	}
	catch (...) {
		if (ErrMsg != "")
			ErrMsg += "\n";
		ErrMsg += "Exception while copying from " + SourceFolder + ": " + "Unrecognized exception occured.";
	}
}

//
// TFolderBackup
//
TFolderBackup::TFolderBackup(const TStr& SettingsFNm)
{
	if (!TFile::Exists(SettingsFNm)) {
		TNotify::StdNotify->OnStatusFmt("Unable to load settings file name for TBackupData. File %s is missing.", SettingsFNm.CStr());
		return;
	}
	TStr JsonData = TStr::LoadTxt(SettingsFNm);
	PJsonVal SettingsJson = TJsonVal::GetValFromStr(JsonData);
	ParseSettings(SettingsJson);
}

TFolderBackup::TFolderBackup(const PJsonVal& SettingsJson)
{
	ParseSettings(SettingsJson);
}

void TFolderBackup::ParseSettings(const PJsonVal& SettingsJson)
{
	// name of the file that holds all backups
	ProfileLogFile = SettingsJson->GetObjStr("backupLogFile");
	
	// load profiles for which we wish to make backups
	PJsonVal ProfilesJson = SettingsJson->GetObjKey("profiles");
	if (!ProfilesJson->IsObj()) {
		TNotify::StdNotify->OnStatus("'profiles' object is not an object");
		return;
	}
	for (int N = 0; N < ProfilesJson->GetObjKeys(); N++) {
		const TStr ProfileName = ProfilesJson->GetObjKey(N);
		ProfileH.AddDat(ProfileName, TBackupProfile(ProfilesJson->GetObjKey(ProfileName)));
	}

	// load logs of the previous backups
	if (TFile::Exists(ProfileLogFile)) {
		PJsonVal LogJson = TJsonVal::GetValFromStr(TStr::LoadTxt(ProfileLogFile));
		if (LogJson->IsObj()) {
			for (int ProfileN = 0; ProfileN < LogJson->GetObjKeys(); ProfileN++) {
				const TStr ProfileName = LogJson->GetObjKey(ProfileN);
				PJsonVal ProfileLog = LogJson->GetObjKey(ProfileName);
				if (!ProfileLog->IsArr()) {
					TNotify::StdNotify->OnStatusFmt("Profile log for %s did not contain an array", ProfileName.CStr());
					continue;
				}
				TVec<TBackupLogInfo>& LogV = ProfileToLogVH.AddDat(ProfileName);
				for (int N = 0; N < ProfileLog->GetArrVals(); N++) {
					PJsonVal Log = ProfileLog->GetArrVal(N);
					LogV.Add(TBackupLogInfo(Log));
				}
			}
		}
	}
}

// backup a particular profile
TBackupLogInfo TFolderBackup::CreateBackup(const TStr& ProfileName)
{
	if (ProfileH.IsKey(ProfileName)) {
		// execute the backup
		TBackupProfile Profile = ProfileH.GetDat(ProfileName);
		TBackupLogInfo Info = Profile.CreateBackup();
		
		// store the log info
		ProfileToLogVH.AddDat(ProfileName).Add(Info);

		// if we have too many backups of the profile, delete the oldest one
		TVec<TBackupLogInfo>& LogV = ProfileToLogVH.GetDat(ProfileName);
		while (LogV.Len() > 1 && LogV.Len() > Profile.GetVersionsToKeep()) {
			const TStr FolderToDelete = Profile.GetDestination() + LogV[0].GetFolderName();
			if (TDir::Exists(FolderToDelete))
				TDir::DelNonEmptyDir(FolderToDelete);
			LogV.Del(0);	// remove the first log item
		}

		// save the file with the logs
		SaveLogs();

		// return
		return Info;
	}
	else
		return TBackupLogInfo("", 0, "Unable to find profile with the specified name");
}

// backup all profiles
void TFolderBackup::CreateBackup(TVec<TBackupLogInfo>& BackupLogInfo)
{
	for (int KeyId = ProfileH.FFirstKeyId(); ProfileH.FNextKeyId(KeyId);) {
		const TStr ProfileName = ProfileH.GetKey(KeyId);
		TBackupLogInfo Info = CreateBackup(ProfileName);
		BackupLogInfo.Add(Info);
	}
}

// save the logs of the backups to the log file
void TFolderBackup::SaveLogs() const
{
	PJsonVal LogsJson =	TJsonVal::NewObj();
	for (int KeyId = ProfileH.FFirstKeyId(); ProfileH.FNextKeyId(KeyId);) {
		const TStr ProfileName = ProfileH.GetKey(KeyId);
		const TVec<TBackupLogInfo>& LogV = ProfileToLogVH.GetDat(ProfileName);
		TJsonValV ArrV;
		for (int N = 0; N < LogV.Len(); N++)
			ArrV.Add(LogV[N].GetJson());
		LogsJson->AddToObj(ProfileName, ArrV);
	}
	TStr JsonStr = LogsJson->SaveStr();
	JsonStr.SaveTxt(ProfileLogFile);
}

// return the list of folders containing backups for a given profile name
// folders are sorted from the oldest to the newest
void TFolderBackup::GetBackupFolders(const TStr& ProfileName, TStrV& FolderNmV) const
{
	FolderNmV.Clr();
	if (ProfileToLogVH.IsKey(ProfileName) && ProfileH.IsKey(ProfileName)) {
		const TVec<TBackupLogInfo>& LogV = ProfileToLogVH.GetDat(ProfileName);
		TBackupProfile Profile = ProfileH.GetDat(ProfileName);
		for (int N = 0; N < LogV.Len(); N++)
			FolderNmV.Add(LogV[N].GetFolderName());
	}
}

// restore a previous backup
// backup folder name is the timestamp of the backed up folder (YYYY-MM-DD HH-MM-SS)
// you can get the folder by calling the GetBackupFolders() method
void TFolderBackup::Restore(const TStr& ProfileName, const TStr& BackupFolderName, const ERestoringMode& RestoringMode) const
{
	if (ProfileToLogVH.IsKey(ProfileName) && ProfileH.IsKey(ProfileName)) {
		const TVec<TBackupLogInfo>& LogV = ProfileToLogVH.GetDat(ProfileName);
		TBackupProfile Profile = ProfileH.GetDat(ProfileName);
		for (int N = 0; N < LogV.Len(); N++) {
			// find the folder that matches the BackupFolderName
			if (LogV[N].GetFolderName() == BackupFolderName) {
				const TVec<TBackupFolderInfo> Folders = Profile.GetFolders();
				for (int N = 0; N < Folders.Len(); N++) {
					const TStr TargetFolder = Folders[N].Folder;
					TStrV PartV; TFile::SplitPath(TargetFolder, PartV);
					const TStr LastFolderNamePart = PartV[PartV.Len() - 1];

					// do we want to first remove any existing data in the target folder?
					if (RestoringMode == RemoveExistingFirst) {
						TDir::DelNonEmptyDir(TargetFolder);
					}

					// copy data from backup to the destination folder
					const TStr SourceFolder = Profile.GetDestination() + BackupFolderName + "/" + LastFolderNamePart;
					if (TDir::Exists(SourceFolder))
						TDir::CopyDir(SourceFolder, TargetFolder, RestoringMode == OverwriteIfExisting);
					else
						TNotify::StdNotify->OnStatusFmt("WARNING: Unable to create a restore of the folder %s. The folder does not exist.", SourceFolder.CStr());
				}
			}
		}
	}
}