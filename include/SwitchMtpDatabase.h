/*
 * Copyright (C) 2013 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef STUB_MTP_DATABASE_H_
#define STUB_MTP_DATABASE_H_

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <tuple>
#include <exception>
#include <algorithm>
#include <filesystem>
#include <sys/stat.h>

#include "mtp.h"
#include "MtpDatabase.h"
#include "MtpDataPacket.h"
#include "MtpStringBuffer.h"
#include "MtpObjectInfo.h"
#include "MtpProperty.h"
#include "MtpDebug.h"

#include "log.h"

#define ALL_PROPERTIES 0xffffffff

using namespace std::filesystem;

namespace android
{
class SwitchMtpDatabase : public android::MtpDatabase {
private:
    struct DbEntry
    {
        MtpStorageID storage_id;
        MtpObjectFormat object_format;
        MtpObjectHandle parent;
        size_t object_size;
        std::string display_name;
        std::string path;
        std::time_t last_modified;
        bool scanned = false;
    };

    MtpServer* local_server;
    uint32_t counter;
    std::map<MtpObjectHandle, DbEntry> db;
    std::map<std::string, MtpObjectFormat> formats = {
        {".gif", MTP_FORMAT_GIF},
        {".png", MTP_FORMAT_PNG},
        {".jpeg", MTP_FORMAT_JFIF},
        {".tiff", MTP_FORMAT_TIFF},
        {".ogg", MTP_FORMAT_OGG},
        {".mp3", MTP_FORMAT_MP3},
        {".wav", MTP_FORMAT_WAV},
        {".wma", MTP_FORMAT_WMA},
        {".aac", MTP_FORMAT_AAC},
        {".flac", MTP_FORMAT_FLAC}
    };

    MtpObjectFormat guess_object_format(std::string extension)
    {
        std::map<std::string, MtpObjectFormat>::iterator it;

        it = formats.find(extension);
        if (it == formats.end()) {
            std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);
            it = formats.find(extension);
            if (it == formats.end()) {
                return MTP_FORMAT_UNDEFINED;
            }
        }

        return it->second;
    }
    
    void add_file_entry(path p, MtpObjectHandle parent, MtpStorageID storage)
    {
        MtpObjectHandle handle = counter;
        DbEntry entry;

        counter++;

        try {
            if (is_directory(p)) {
                entry.storage_id = storage;
                entry.parent = parent;
                entry.display_name = std::string(p.filename().string());
                entry.path = p.string();
                entry.object_format = MTP_FORMAT_ASSOCIATION;
                entry.object_size = 0;

                struct stat result;
                stat(p.string().c_str(), &result);
                entry.last_modified = result.st_mtime;
                db.insert( std::pair<MtpObjectHandle, DbEntry>(handle, entry) );

                if (local_server)
                    local_server->sendObjectAdded(handle);

            } else {
                try {
                    entry.storage_id = storage;
                    entry.parent = parent;
                    entry.display_name = std::string(p.filename().string());
                    entry.path = p.string();
                    entry.object_format = guess_object_format(p.extension().string());
                    entry.object_size = file_size(p);
                    struct stat result;
                    stat(p.string().c_str(), &result);
                    entry.last_modified = result.st_mtime;

                    VLOG(1) << "Adding \"" << p.string() << "\"";

                    db.insert( std::pair<MtpObjectHandle, DbEntry>(handle, entry) );

                    if (local_server)
                        local_server->sendObjectAdded(handle);

                } catch (const filesystem_error& ex) {
                    LOG(WARNING) << "There was an error reading file properties";
                }
            }
        } catch (const filesystem_error& ex) {
            LOG(ERROR) << ex.what();
        }
    }

    void parse_directory(path p, MtpObjectHandle parent, MtpStorageID storage)
    {
        DbEntry entry;
        std::vector<path> v;
        directory_iterator i(p);
        copy(i, directory_iterator(), std::back_inserter(v));

        for (std::vector<path>::const_iterator it(v.begin()), it_end(v.end()); it != it_end; ++it)
        {
            add_file_entry(*it, parent, storage);
        }

        if (db.find(parent) != db.end())
            db.at(parent).scanned = true;
    }

    void readFiles(const std::string& sourcedir, const std::string& display, MtpStorageID storage, bool hidden)
    {
        path p (sourcedir);
        DbEntry entry;
        MtpObjectHandle handle = counter++;
        std::string display_name = std::string(p.filename().string());

        if (!display.empty())
            display_name = display;

        try {
            if (exists(p)) {
                if (is_directory(p)) {
                    entry.storage_id = storage;
                    entry.parent = hidden ? MTP_PARENT_ROOT : 0;
                    entry.display_name = display_name;
                    entry.path = p.string();
                    entry.object_format = MTP_FORMAT_ASSOCIATION;
                    entry.object_size = 0;
                    struct stat result;
                    stat(p.string().c_str(), &result);
                    entry.last_modified = result.st_mtime;

                    db.insert( std::pair<MtpObjectHandle, DbEntry>(handle, entry) );

                    parse_directory (p, hidden ? 0 : handle, storage);
                } else
                    LOG(WARNING) << p << " is not a directory.";
            } else {
                if (storage == MTP_STORAGE_FIXED_RAM)
                    LOG(WARNING) << p << " does not exist.";
                else {
                    entry.storage_id = storage;
                    entry.parent = -1;
                    entry.display_name = display_name;
                    entry.path = p.parent_path().string();
                    entry.object_format = MTP_FORMAT_ASSOCIATION;
                    entry.object_size = 0;
                    entry.last_modified = 0;
                }
            }
        }
        catch (const filesystem_error& ex) {
            LOG(ERROR) << ex.what();
        }
    }

public:

    SwitchMtpDatabase() :
      counter(1)
    {
        local_server = nullptr;
        db = std::map<MtpObjectHandle, DbEntry>();
    }

    virtual ~SwitchMtpDatabase() {
    }

    virtual bool isHandleValid(MtpObjectHandle handle) {
        return handle > 0 && handle < counter;
    }

    virtual void addStoragePath(const MtpString& path,
                                const MtpString& displayName,
                                MtpStorageID storage,
                                bool hidden)
    {
        readFiles(path, displayName, storage, hidden);
    }

    virtual void removeStorage(MtpStorageID storage)
    {
        // remove all database entries corresponding to said storage.
        for(std::map<MtpObjectHandle, DbEntry>::iterator it = db.begin(); it != db.end(); ++it) {
            if (it->second.storage_id == storage)
                db.erase(it);
        }
    }

    // called from SendObjectInfo to reserve a database entry for the incoming file
    virtual MtpObjectHandle beginSendObject(
        const MtpString& path,
        MtpObjectFormat format,
        MtpObjectHandle parent,
        MtpStorageID storage,
        uint64_t size,
        time_t modified)
    {
        DbEntry entry;
        MtpObjectHandle handle = counter;

        if (storage == MTP_STORAGE_FIXED_RAM && parent == 0)
            return kInvalidObjectHandle;

        VLOG(1) << __PRETTY_FUNCTION__ << ": " << path << " - " << parent
                << " format: " << std::hex << format << std::dec;

        entry.storage_id = storage;
        entry.parent = parent;
        entry.display_name = std::filesystem::path(path).filename().string();
        entry.path = path;
        entry.object_format = format;
        entry.object_size = size;
        entry.last_modified = modified;

        db.insert( std::pair<MtpObjectHandle, DbEntry>(handle, entry) );

        counter++;

        return handle; 
    }

    // called to report success or failure of the SendObject file transfer
    // success should signal a notification of the new object's creation,
    // failure should remove the database entry created in beginSendObject
    virtual void endSendObject(
        const MtpString& path,
        MtpObjectHandle handle,
        MtpObjectFormat format,
        bool succeeded)
    {
        VLOG(1) << __PRETTY_FUNCTION__ << ": " << path;

        try
        {
            if (!succeeded) {
                db.erase(handle);
            } else {
                std::filesystem::path p (path);

                if (format != MTP_FORMAT_ASSOCIATION) {
                    /* Resync file size, just in case this is actually an Edit. */
                    db.at(handle).object_size = file_size(p);
                }
            }
        } catch(...)
        {
            LOG(ERROR) << __PRETTY_FUNCTION__
                       << ": failed to complete object creation:" << path;
        }
    }

    virtual MtpObjectHandleList* getObjectList(
        MtpStorageID storageID,
        MtpObjectFormat format,
        MtpObjectHandle parent)
    {
        VLOG(1) << __PRETTY_FUNCTION__ << ": " << storageID << ", " << format << ", " << parent;
        MtpObjectHandleList* list = nullptr;

        if (parent == MTP_PARENT_ROOT)
            parent = 0;
        
        // Scan unscanned directories
        else if (isHandleValid(parent) && !db.at(parent).scanned)
            parse_directory (db.at(parent).path, parent, storageID);

        try
        {
            std::vector<MtpObjectHandle> keys;

            for(std::map<MtpObjectHandle, DbEntry>::iterator it = db.begin(); it != db.end(); ++it) {
                if (it->second.storage_id == storageID && it->second.parent == parent)
                    if (format == 0 || it->second.object_format == format)
                        keys.push_back(it->first);
            }

            list = new MtpObjectHandleList(keys);
        } catch(...)
        {
            list = new MtpObjectHandleList();
        }
        
        return list;
    }

    virtual int getNumObjects(
        MtpStorageID storageID,
        MtpObjectFormat format,
        MtpObjectHandle parent)
    {
        VLOG(1) << __PRETTY_FUNCTION__ << ": " << storageID << ", " << format << ", " << parent;

        int result = 0;

        try
        {
            MtpObjectHandleList *list = getObjectList(storageID, format, parent);
            result = list->size();
            delete list;
        } catch(...)
        {
        }
        
        return result;
    }

    // callee should delete[] the results from these
    // results can be NULL
    virtual MtpObjectFormatList* getSupportedPlaybackFormats()
    {
        VLOG(1) << __PRETTY_FUNCTION__;
        static const MtpObjectFormatList list = {
            /* Generic files */
            MTP_FORMAT_UNDEFINED,
            MTP_FORMAT_ASSOCIATION, // folders
            MTP_FORMAT_TEXT,
            MTP_FORMAT_HTML,

            /* Supported image formats */
            MTP_FORMAT_DEFINED, // generic image
            MTP_FORMAT_EXIF_JPEG,
            MTP_FORMAT_TIFF_EP,
            MTP_FORMAT_BMP,
            MTP_FORMAT_GIF,
            MTP_FORMAT_JFIF,
            MTP_FORMAT_PNG,
            MTP_FORMAT_TIFF,
            MTP_FORMAT_TIFF_IT,
            MTP_FORMAT_JP2,
            MTP_FORMAT_JPX,

            /* Supported audio formats */
            MTP_FORMAT_OGG,
            MTP_FORMAT_MP3,
            MTP_FORMAT_WAV,
            MTP_FORMAT_WMA,
            MTP_FORMAT_AAC,
            MTP_FORMAT_FLAC,

            /* Supported video formats */
            // none listed yet, video apparently broken.

            /* Audio album, and album art */
            MTP_FORMAT_ABSTRACT_AUDIO_ALBUM,

            /* Playlists for audio and video */
            MTP_FORMAT_ABSTRACT_AV_PLAYLIST,
        };

        return new MtpObjectFormatList{list};
    }
    
    virtual MtpObjectFormatList* getSupportedCaptureFormats()
    {
        VLOG(1) << __PRETTY_FUNCTION__;
        static const MtpObjectFormatList list = {MTP_FORMAT_ASSOCIATION, MTP_FORMAT_PNG};
        return new MtpObjectFormatList{list};
    }
    
    virtual MtpObjectPropertyList* getSupportedObjectProperties(MtpObjectFormat format)
    {
        VLOG(1) << __PRETTY_FUNCTION__;
        /*
        if (format != MTP_FORMAT_PNG)
            return nullptr;
        */
            
        static const MtpObjectPropertyList list = 
        {
            MTP_PROPERTY_STORAGE_ID,
            MTP_PROPERTY_PARENT_OBJECT,
            MTP_PROPERTY_OBJECT_FORMAT,
            MTP_PROPERTY_OBJECT_SIZE,
            MTP_PROPERTY_OBJECT_FILE_NAME,
            MTP_PROPERTY_DISPLAY_NAME,
            MTP_PROPERTY_PERSISTENT_UID,
            MTP_PROPERTY_ASSOCIATION_TYPE,
            MTP_PROPERTY_ASSOCIATION_DESC,
            MTP_PROPERTY_PROTECTION_STATUS,
            MTP_PROPERTY_DATE_CREATED,
            MTP_PROPERTY_DATE_MODIFIED,
            MTP_PROPERTY_HIDDEN,
            MTP_PROPERTY_NON_CONSUMABLE,

        };
         
        return new MtpObjectPropertyList{list};
    }
    
    virtual MtpDevicePropertyList* getSupportedDeviceProperties()
    {
        VLOG(1) << __PRETTY_FUNCTION__;
        static const MtpDevicePropertyList list = {
            MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME,
            MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER, 
        };
        return new MtpDevicePropertyList{list};
    }

    virtual MtpResponseCode getObjectPropertyValue(
        MtpObjectHandle handle,
        MtpObjectProperty property,
        MtpDataPacket& packet)
    {        
        char date[20];

        VLOG(1) << __PRETTY_FUNCTION__
                << " handle: " << handle
                << " property: " << MtpDebug::getObjectPropCodeName(property);

        if (handle == MTP_PARENT_ROOT || handle == 0)
            return MTP_RESPONSE_INVALID_OBJECT_HANDLE;

        try {
            switch(property)
            {
                case MTP_PROPERTY_STORAGE_ID: packet.putUInt32(db.at(handle).storage_id); break;            
                case MTP_PROPERTY_PARENT_OBJECT: packet.putUInt32(db.at(handle).parent); break;            
                case MTP_PROPERTY_OBJECT_FORMAT: packet.putUInt16(db.at(handle).object_format); break;
                case MTP_PROPERTY_OBJECT_SIZE: packet.putUInt32(db.at(handle).object_size); break;
                case MTP_PROPERTY_DISPLAY_NAME: packet.putString(db.at(handle).display_name.c_str()); break;
                case MTP_PROPERTY_OBJECT_FILE_NAME: packet.putString(db.at(handle).display_name.c_str()); break;
                case MTP_PROPERTY_PERSISTENT_UID: packet.putUInt128(handle); break;
                case MTP_PROPERTY_ASSOCIATION_TYPE:
                    if (db.at(handle).object_format == MTP_FORMAT_ASSOCIATION)
                        packet.putUInt16(MTP_ASSOCIATION_TYPE_GENERIC_FOLDER);
                    else
                        packet.putUInt16(0);
                    break;
                case MTP_PROPERTY_ASSOCIATION_DESC: packet.putUInt32(0); break;
                case MTP_PROPERTY_PROTECTION_STATUS:
                    packet.putUInt16(0x0000); // no files are read-only for now.
                    break;
                case MTP_PROPERTY_DATE_CREATED:
                    formatDateTime(0, date, sizeof(date));
                    packet.putString(date);
                    break;
                case MTP_PROPERTY_DATE_MODIFIED:
                    formatDateTime(db.at(handle).last_modified, date, sizeof(date));
                    packet.putString(date);
                    break;
                case MTP_PROPERTY_HIDDEN: packet.putUInt16(0); break;
                case MTP_PROPERTY_NON_CONSUMABLE: break;
                    if (db.at(handle).object_format == MTP_FORMAT_ASSOCIATION)
                        packet.putUInt16(0); // folders are non-consumable
                    else
                        packet.putUInt16(1); // files can usually be played.
                    break;
                default: return MTP_RESPONSE_GENERAL_ERROR; break;                
            }
        
            return MTP_RESPONSE_OK;
        }
        catch (...) {
            LOG(ERROR) << __PRETTY_FUNCTION__
                       << "Could not retrieve property: "
                       << MtpDebug::getObjectPropCodeName(property)
                       << " for handle: " << handle;
            return MTP_RESPONSE_GENERAL_ERROR;
        }
    }

    virtual MtpResponseCode setObjectPropertyValue(
        MtpObjectHandle handle,
        MtpObjectProperty property,
        MtpDataPacket& packet)
    {
        DbEntry entry;
        MtpStringBuffer buffer;
        std::string oldname;
        std::string newname;
        path oldpath;
        path newpath;

        VLOG(1) << __PRETTY_FUNCTION__
                << " handle: " << handle
                << " property: " << MtpDebug::getObjectPropCodeName(property);

        if (handle == MTP_PARENT_ROOT || handle == 0)
            return MTP_RESPONSE_INVALID_OBJECT_HANDLE;

        switch(property)
        {
            case MTP_PROPERTY_OBJECT_FILE_NAME:
                try {
                    entry = db.at(handle);

                    packet.getString(buffer);
                    newname = strdup(buffer);

                    oldpath /= entry.path;
                    newpath /= oldpath.parent_path() / newname; //TODO: compare with branch_path

                    rename(oldpath, newpath);

                    db.at(handle).display_name = newname;
                    db.at(handle).path = newpath.string();
                } catch (filesystem_error& fe) {
                    LOG(ERROR) << fe.what();
                    return MTP_RESPONSE_DEVICE_BUSY;
                } catch (std::exception& e) {
                    LOG(ERROR) << e.what();
                    return MTP_RESPONSE_GENERAL_ERROR;
                } catch (...) {
                    LOG(ERROR) << "An unexpected error has occurred";
                    return MTP_RESPONSE_GENERAL_ERROR;
                }

                break;
            case MTP_PROPERTY_PARENT_OBJECT:
                try {
                    entry = db.at(handle);
                    entry.parent = packet.getUInt32();
                }
                catch (...) {
                    LOG(ERROR) << "Could not change parent object for handle "
                               << handle;
                    return MTP_RESPONSE_GENERAL_ERROR;
                }
            default: return MTP_RESPONSE_OPERATION_NOT_SUPPORTED; break;
        }
        
        return MTP_RESPONSE_OK;
    }

    virtual MtpResponseCode getDevicePropertyValue(
        MtpDeviceProperty property,
        MtpDataPacket& packet)
    {
        VLOG(1) << __PRETTY_FUNCTION__;
        switch(property)
        {
            case MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER:
            case MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME:
                packet.putString("");
                break;
            default: return MTP_RESPONSE_OPERATION_NOT_SUPPORTED; break;
        }
        
        return MTP_RESPONSE_OK;
    }

    virtual MtpResponseCode setDevicePropertyValue(
        MtpDeviceProperty property,
        MtpDataPacket& packet)
    {
        VLOG(1) << __PRETTY_FUNCTION__;
        return MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED;
    }

    virtual MtpResponseCode resetDeviceProperty(
        MtpDeviceProperty property)
    {
        VLOG(1) << __PRETTY_FUNCTION__;
        return MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED;
    }

    virtual MtpResponseCode getObjectPropertyList(
        MtpObjectHandle handle,
        uint32_t format, 
        uint32_t property,
        int groupCode, 
        int depth,
        MtpDataPacket& packet)
    {
        std::vector<MtpObjectHandle> handles;

        VLOG(2) << __PRETTY_FUNCTION__;

        if (handle == kInvalidObjectHandle)
            return MTP_RESPONSE_PARAMETER_NOT_SUPPORTED;

        if (property == 0 && groupCode == 0)
            return MTP_RESPONSE_PARAMETER_NOT_SUPPORTED;

        if (groupCode != 0)
            return MTP_RESPONSE_SPECIFICATION_BY_GROUP_UNSUPPORTED;

        if (depth > 1)
            return MTP_RESPONSE_SPECIFICATION_BY_DEPTH_UNSUPPORTED;

        if (depth == 0) {
            /* For a depth search, a handle of 0 is valid (objects at the root)
             * but it isn't when querying for the properties of a single object.
             */
            if (db.find(handle) == db.end())
                return MTP_RESPONSE_INVALID_OBJECT_HANDLE;

            handles.push_back(handle);
        } else {
            for(std::map<MtpObjectHandle, DbEntry>::iterator it = db.begin(); it != db.end(); ++it) {
                if (it->second.parent == handle)
                    handles.push_back(it->first);
            }
        }

        /*
         * getObjectPropList returns an ObjectPropList dataset table;
         * built as such:
         *
         * 1- Number of elements (quadruples)
         * a1- Element 1 Object Handle
         * a2- Element 1 Property Code
         * a3- Element 1 Data type
         * a4- Element 1 Value
         * b... rinse, repeat.
         */

        if (property == ALL_PROPERTIES)
             packet.putUInt32(6 * handles.size());
        else
             packet.putUInt32(1 * handles.size());

        for(std::vector<MtpObjectHandle>::iterator it = handles.begin(); it != handles.end(); ++it) {
            MtpObjectHandle i = *it;
            DbEntry entry = db.at(i);

            // Persistent Unique Identifier.
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_PERSISTENT_UID) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_PERSISTENT_UID);
                packet.putUInt16(MTP_TYPE_UINT128);
                packet.putUInt128(i);
            }

            // Storage ID
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_STORAGE_ID) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_STORAGE_ID);
                packet.putUInt16(MTP_TYPE_UINT32);
                packet.putUInt32(entry.storage_id);
            }

            // Parent
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_PARENT_OBJECT) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_PARENT_OBJECT);
                packet.putUInt16(MTP_TYPE_UINT32);
                packet.putUInt32(entry.parent);
            }

            // Object Format
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_OBJECT_FORMAT) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_OBJECT_FORMAT);
                packet.putUInt16(MTP_TYPE_UINT16);
                packet.putUInt16(entry.object_format);
            }

            // Object Size
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_OBJECT_SIZE) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_OBJECT_SIZE);
                packet.putUInt16(MTP_TYPE_UINT32);
                packet.putUInt32(entry.object_size);
            }

            // Object File Name
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_OBJECT_FILE_NAME) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_OBJECT_FILE_NAME);
                packet.putUInt16(MTP_TYPE_STR);
                packet.putString(entry.display_name.c_str());
            }

            // Display Name
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_DISPLAY_NAME) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_DISPLAY_NAME);
                packet.putUInt16(MTP_TYPE_STR);
                packet.putString(entry.display_name.c_str());
            }

            // Association Type
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_ASSOCIATION_TYPE) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_ASSOCIATION_TYPE);
                packet.putUInt16(MTP_TYPE_UINT16);
                if (entry.object_format == MTP_FORMAT_ASSOCIATION)
                    packet.putUInt16(MTP_ASSOCIATION_TYPE_GENERIC_FOLDER);
                else
                    packet.putUInt16(0);
            }

            // Association Description
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_ASSOCIATION_DESC) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_ASSOCIATION_DESC);
                packet.putUInt16(MTP_TYPE_UINT32);
                packet.putUInt32(0);
            }

            // Protection Status
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_PROTECTION_STATUS) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_PROTECTION_STATUS);
                packet.putUInt16(MTP_TYPE_UINT16);
                packet.putUInt16(0x0000); //FIXME: all files are read-write for now
                // packet.putUInt16(0x8001);
            }

            // Date Created
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_DATE_CREATED) {
                char date[20];
                formatDateTime(0, date, sizeof(date));
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_DATE_CREATED);
                packet.putUInt16(MTP_TYPE_STR);
                packet.putString(date);
            }

            // Date Modified
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_DATE_MODIFIED) {
                char date[20];
                formatDateTime(entry.last_modified, date, sizeof(date));
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_DATE_CREATED);
                packet.putUInt16(MTP_TYPE_STR);
                packet.putString(date);
            }

            // Hidden
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_HIDDEN) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_HIDDEN);
                packet.putUInt16(MTP_TYPE_UINT16);
                packet.putUInt16(0);
            }

            // Non Consumable
            if (property == ALL_PROPERTIES || property == MTP_PROPERTY_NON_CONSUMABLE) {
                packet.putUInt32(i);
                packet.putUInt16(MTP_PROPERTY_NON_CONSUMABLE);
                packet.putUInt16(MTP_TYPE_UINT16);
                if (entry.object_format == MTP_FORMAT_ASSOCIATION)
                    packet.putUInt16(0); // folders are non-consumable
                else
                    packet.putUInt16(1); // files can usually be played.
                break;
            }

        }

        return MTP_RESPONSE_OK;
    }

    virtual MtpResponseCode getObjectInfo(
        MtpObjectHandle handle,
        MtpObjectInfo& info)
    {
        VLOG(2) << __PRETTY_FUNCTION__;

        if (handle == 0 || handle == MTP_PARENT_ROOT)
            return MTP_RESPONSE_INVALID_OBJECT_HANDLE;

        try {
            info.mHandle = handle;
            info.mStorageID = db.at(handle).storage_id;
            info.mFormat = db.at(handle).object_format;
            info.mProtectionStatus = 0x0;
            info.mCompressedSize = db.at(handle).object_size;
            info.mImagePixWidth = 0;
            info.mImagePixHeight = 0;
            info.mImagePixDepth = 0;
            info.mParent = db.at(handle).parent;
            info.mAssociationType
                = info.mFormat == MTP_FORMAT_ASSOCIATION
                    ? MTP_ASSOCIATION_TYPE_GENERIC_FOLDER : 0;
            info.mAssociationDesc = 0;
            info.mSequenceNumber = 0;
            info.mName = ::strdup(db.at(handle).display_name.c_str());
            info.mDateCreated = 0;
            info.mDateModified = db.at(handle).last_modified;
            info.mKeywords = ::strdup("ubuntu,touch");

            if (VLOG_IS_ON(2))
                info.print();

            return MTP_RESPONSE_OK;
        }
        catch (...) {
            return MTP_RESPONSE_GENERAL_ERROR;
        }
    }

    virtual void* getThumbnail(MtpObjectHandle handle, size_t& outThumbSize)
    {
        void* result = malloc(outThumbSize);

        outThumbSize = 0;
        memset(result, 0, outThumbSize);

        return result;
    }

    virtual MtpResponseCode getObjectFilePath(
        MtpObjectHandle handle,
        MtpString& outFilePath,
        int64_t& outFileLength,
        MtpObjectFormat& outFormat)
    {
        VLOG(1) << __PRETTY_FUNCTION__ << " handle: " << handle;

        if (handle == 0 || handle == MTP_PARENT_ROOT)
            return MTP_RESPONSE_INVALID_OBJECT_HANDLE;

        try {
            DbEntry entry = db.at(handle);

            VLOG(2) << __PRETTY_FUNCTION__
                    << "handle: " << handle
                    << "path: " << entry.path
                    << "length: " << entry.object_size
                    << "format: " << entry.object_format;

            outFilePath = std::string(entry.path);
            outFileLength = entry.object_size;
            outFormat = entry.object_format;

            return MTP_RESPONSE_OK;
        }
        catch (...) {
            return MTP_RESPONSE_GENERAL_ERROR;
        }
    }

    virtual MtpResponseCode deleteFile(MtpObjectHandle handle)
    {
        size_t orig_size = db.size();
        size_t new_size;

        VLOG(2) << __PRETTY_FUNCTION__ << " handle: " << handle;

        if (handle == 0 || handle == MTP_PARENT_ROOT)
            return MTP_RESPONSE_INVALID_OBJECT_HANDLE;

        try {
            new_size = db.erase(handle);

            if (orig_size > new_size) {
                /* Recursively remove children object from the DB as well.
                 * we can safely ignore failures here, since the objects
                 * would not be reachable anyway.
                 */
                for(std::map<MtpObjectHandle, DbEntry>::iterator it = db.begin(); it != db.end(); ++it) {
                    if (it->second.parent == handle)
                        db.erase(it);
                }

                return MTP_RESPONSE_OK;
            }
            else
                return MTP_RESPONSE_GENERAL_ERROR;
        }
        catch (...) {
            return MTP_RESPONSE_GENERAL_ERROR;
        }
    }

    virtual MtpResponseCode moveFile(MtpObjectHandle handle, MtpObjectHandle new_parent)
    {
        VLOG(1) << __PRETTY_FUNCTION__ << " handle: " << handle
                << " new parent: " << new_parent;

        if (handle == 0 || handle == MTP_PARENT_ROOT)
            return MTP_RESPONSE_INVALID_OBJECT_HANDLE;

        try {
            // change parent
            db.at(handle).parent = new_parent;
        }
        catch (...) {
            return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
        }

        return MTP_RESPONSE_OK;
    }

    /*
    virtual MtpResponseCode copyFile(MtpObjectHandle handle, MtpObjectHandle new_parent)
    {
        VLOG(2) << __PRETTY_FUNCTION__;

        // duplicate DbEntry
        // change parent

        return MTP_RESPONSE_OK
    }
    */

    virtual MtpObjectHandleList* getObjectReferences(MtpObjectHandle handle)
    {
        VLOG(1) << __PRETTY_FUNCTION__;

        if (handle == 0 || handle == MTP_PARENT_ROOT)
            return nullptr;

        return getObjectList(db.at(handle).storage_id,
                             handle,
                             db.at(handle).object_format);
    }

    virtual MtpResponseCode setObjectReferences(
        MtpObjectHandle handle,
        MtpObjectHandleList* references)
    {
        VLOG(1) << __PRETTY_FUNCTION__;

        // ignore, we don't keep the references in a list.

        return MTP_RESPONSE_OK;
    }

    virtual MtpProperty* getObjectPropertyDesc(
        MtpObjectProperty property,
        MtpObjectFormat format)
    {
        VLOG(1) << __PRETTY_FUNCTION__ << MtpDebug::getObjectPropCodeName(property);

        MtpProperty* result = nullptr;
        switch(property)
        {
            case MTP_PROPERTY_STORAGE_ID: result = new MtpProperty(property, MTP_TYPE_UINT32, false); break;
            case MTP_PROPERTY_PARENT_OBJECT: result = new MtpProperty(property, MTP_TYPE_UINT32, true); break;
            case MTP_PROPERTY_OBJECT_FORMAT: result = new MtpProperty(property, MTP_TYPE_UINT16, false); break;
            case MTP_PROPERTY_OBJECT_SIZE: result = new MtpProperty(property, MTP_TYPE_UINT32, false); break;
            case MTP_PROPERTY_WIDTH: result = new MtpProperty(property, MTP_TYPE_UINT32, false); break;
            case MTP_PROPERTY_HEIGHT: result = new MtpProperty(property, MTP_TYPE_UINT32, false); break;
            case MTP_PROPERTY_IMAGE_BIT_DEPTH: result = new MtpProperty(property, MTP_TYPE_UINT32, false); break;
            case MTP_PROPERTY_DISPLAY_NAME: result = new MtpProperty(property, MTP_TYPE_STR, true); break;
            case MTP_PROPERTY_OBJECT_FILE_NAME: result = new MtpProperty(property, MTP_TYPE_STR, true); break;
            case MTP_PROPERTY_PERSISTENT_UID: result = new MtpProperty(property, MTP_TYPE_UINT128, false); break;
            case MTP_PROPERTY_ASSOCIATION_TYPE: result = new MtpProperty(property, MTP_TYPE_UINT16, false); break;
            case MTP_PROPERTY_ASSOCIATION_DESC: result = new MtpProperty(property, MTP_TYPE_UINT32, false); break;
            case MTP_PROPERTY_PROTECTION_STATUS: result = new MtpProperty(property, MTP_TYPE_UINT16, false); break;
            case MTP_PROPERTY_DATE_CREATED: result = new MtpProperty(property, MTP_TYPE_STR, false); break;
            case MTP_PROPERTY_DATE_MODIFIED: result = new MtpProperty(property, MTP_TYPE_STR, false); break;
            case MTP_PROPERTY_HIDDEN: result = new MtpProperty(property, MTP_TYPE_UINT16, false); break;
            case MTP_PROPERTY_NON_CONSUMABLE: result = new MtpProperty(property, MTP_TYPE_UINT16, false); break;
            default: break;                
        }
        
        return result;
    }

    virtual MtpProperty* getDevicePropertyDesc(MtpDeviceProperty property)
    {
        VLOG(1) << __PRETTY_FUNCTION__ << MtpDebug::getDevicePropCodeName(property);

        MtpProperty* result = nullptr;
        switch(property)
        {
            case MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER:
            case MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME:
                result = new MtpProperty(property, MTP_TYPE_STR, false); break;
            default: break;                
        }
        
        return result;
    }
    
    virtual void sessionStarted(MtpServer* server)
    {
        VLOG(1) << __PRETTY_FUNCTION__;
        local_server = server;
    }

    virtual void sessionEnded()
    {
        VLOG(1) << __PRETTY_FUNCTION__;
        VLOG(1) << "objects in db at session end: " << db.size();
        local_server = nullptr;
    }
};
}

#endif // STUB_MTP_DATABASE_H_
