#pragma once

#include "cmndef.h"
#include "iodevice.h"
#include "../../bzscore/string.h"
#include <list>
#include <vector>
#include <map>

#define PDO_HANDLER_DECL	__forceinline

namespace BazisLib
{
	namespace DDK
	{
		class IUnknownDeviceInterface
		{
		public:
			virtual unsigned AddRef()=0;
			virtual unsigned Release()=0;
			virtual void *QueryInterface(LPCGUID lpGuid, unsigned Version, PVOID pSpecificData)=0;
		};

		class BusDevice : public IODevice
		{
		public:
			class PDOBase;

		private:
			std::list<PDOBase *> m_PhysicalDevObjects;
			FastMutex m_ListAccessMutex;
			long m_NextDeviceNumber;
			String m_BusPrefix;

		public:
			BusDevice(String BusPrefix,
					  bool bDeleteThisAfterRemoveRequest = false,
					  ULONG DeviceCharacteristics = FILE_DEVICE_SECURE_OPEN,
					  bool bExclusive = FALSE,
					  ULONG AdditionalDeviceFlags = DO_POWER_PAGABLE);

			virtual ~BusDevice();

			virtual void OnPDORequestedPlugout(PDOBase *pPDO);

		protected:
			virtual NTSTATUS OnSurpriseRemoval() override;
			virtual NTSTATUS OnRemoveDevice(NTSTATUS LowerDeviceRemovalStatus) override;

		protected:
			NTSTATUS AddPDO(PDOBase *pPDO = NULL, OUT LONG *pUniqueID = NULL);

			PDOBase *FindPDOByUniqueID(LONG UniqueID);
			NTSTATUS QueryParentPDOCapabilities(PDEVICE_CAPABILITIES Capabilities);

		protected:
			virtual NTSTATUS OnQueryDeviceRelations(DEVICE_RELATION_TYPE Type, PDEVICE_RELATIONS *ppDeviceRelations);

		public:
			//TODO: store list iterator for faster deletion
			class PDOBase
			{
			private:
				friend class BusDevice;
				template <class _BaseDeviceClass> friend class _BusPDOT;

				BusDevice *m_pBusDevice;

				long m_UniqueID;

				//! Actual device is present. Set to false on ejection and removal
				bool m_bDevicePresent;

				//! The I/O manager no longer considers the device to be one of the child devices for bus FDO
				//! This means that either the FDO handled IRP_MN_QUERY_DEVICE_RELATIONS with m_bDevicePresent = false,
				//! or bus FDO is being removed or received surprise removal IRP.
				bool m_bReportedMissing;

			protected:
				String m_DeviceID;
				String m_HardwareIDs;
				String m_InstanceID;
				String m_CompatibleIDs;

				String m_DeviceType;
				String m_DeviceName;
				String m_DeviceLocation;

			private:
				static void sInterfaceReference(PVOID Context);
				static void sInterfaceDereference(PVOID Context);

			private:
				PDO_HANDLER_DECL PDOBase(const wchar_t *pwszDeviceType = L"StandardDevice", const wchar_t *pwszDeviceName = L"Unknown device", const wchar_t *pwszLocation = L"Unknown");
				virtual ~PDOBase();
			public:

				NTSTATUS Eject();
				//! Notifies Windows that the device is no longer present
				void PlugOut();

			protected:
				//virtual NTSTATUS PDO_HANDLER_DECL OnRemoveDevice(NTSTATUS LowerDeviceRemovalStatus);
				virtual NTSTATUS PDO_HANDLER_DECL OnQueryDeviceRelations(DEVICE_RELATION_TYPE Type, PDEVICE_RELATIONS *ppDeviceRelations);

				virtual NTSTATUS PDO_HANDLER_DECL OnQueryCapabilities(PDEVICE_CAPABILITIES Capabilities);
				virtual NTSTATUS PDO_HANDLER_DECL OnQueryDeviceID(BUS_QUERY_ID_TYPE Type, PWCHAR *ppDeviceID);
				virtual NTSTATUS PDO_HANDLER_DECL OnQueryDeviceText(DEVICE_TEXT_TYPE Type, LCID Locale, PWCHAR *ppDeviceText);
				virtual NTSTATUS PDO_HANDLER_DECL OnQueryResources(PCM_RESOURCE_LIST *ppResourceList);
				virtual NTSTATUS PDO_HANDLER_DECL OnQueryResourceRequirements(PIO_RESOURCE_REQUIREMENTS_LIST *ppResourceRequirementsList);
				virtual NTSTATUS PDO_HANDLER_DECL OnQueryBusInformation(PPNP_BUS_INFORMATION *ppBusInformation);
				virtual NTSTATUS PDO_HANDLER_DECL OnEject();

				virtual NTSTATUS PDO_HANDLER_DECL OnQueryInterfaceRaw(CONST GUID *InterfaceType, USHORT Size, USHORT Version, PINTERFACE pInterface, PVOID InterfaceSpecificData);

				NTSTATUS TryDispatchPNP(IN IncomingIrp *Irp, IO_STACK_LOCATION *IrpSp);

				//! Requests the corresponding bus driver to plug out the device
				void RequestBusToPlugOut();

			protected:
				BusDevice* GetBusDevice() {return m_pBusDevice;}
				bool WasReportedMissing() {return m_bReportedMissing;}

				void RemoveThisFromBusDeviceList()
				{
					FastMutexLocker lck(m_pBusDevice->m_ListAccessMutex);
					m_pBusDevice->m_PhysicalDevObjects.remove(this);
				}

			protected:
				virtual void GenerateIDStrings(int UniqueDeviceNumber);
				virtual IUnknownDeviceInterface *CreateObjectByInterfaceID(LPCGUID lpGuid, unsigned Version, PVOID pSpecificData) {return NULL;}

			protected:
				virtual PDEVICE_OBJECT GetDeviceObjectForPDO() = 0;
				virtual DevicePNPState GetCurrentPNPStateForPDO() = 0;
				virtual NTSTATUS Register(Driver *pDriver) = 0;

			public:
				unsigned GetUniqueID() {return m_UniqueID;}
			};
		};

		//! Provides a base class for _BusPDOT, that has a default constructur
		template <DEVICE_TYPE _DeviceType = FILE_DEVICE_BUS_EXTENDER,
				  bool _DeleteThisAfterRemoveRequest = true,
				  ULONG _DeviceCharacteristics = FILE_AUTOGENERATED_DEVICE_NAME |FILE_DEVICE_SECURE_OPEN>
					class _IODeviceT : public IODevice
		{
		protected:
			_IODeviceT()
				: IODevice(_DeviceType, _DeleteThisAfterRemoveRequest, _DeviceCharacteristics, FALSE)

			{
			}
		};

		template <class _BaseDeviceClass = _IODeviceT<>> class _BusPDOT : public BusDevice::PDOBase, public _BaseDeviceClass
		{
		public:
			PDO_HANDLER_DECL _BusPDOT(const wchar_t *pwszDeviceType = L"StandardDevice", const wchar_t *pwszDeviceName = L"Unknown device", const wchar_t *pwszLocation = L"Unknown")
				: BusDevice::PDOBase(pwszDeviceType, pwszDeviceName, pwszLocation)
			{
			}

			virtual NTSTATUS DispatchPNP(IN IncomingIrp *Irp, IO_STACK_LOCATION *IrpSp) override
			{
				NTSTATUS status = BusDevice::PDOBase::TryDispatchPNP(Irp, IrpSp);
				if (status != STATUS_NOT_SUPPORTED)
					return status;
				return _BaseDeviceClass::DispatchPNP(Irp, IrpSp);
			}

		private:
			virtual PDEVICE_OBJECT GetDeviceObjectForPDO() override
			{
				return _BaseDeviceClass::GetDeviceObject();
			}

			virtual DevicePNPState GetCurrentPNPStateForPDO() override
			{
				return _BaseDeviceClass::GetCurrentPNPState();
			}

			virtual NTSTATUS Register(Driver *pDriver) override
			{
				return AddDevice(pDriver, NULL);
			}

			NTSTATUS OnRemoveDevice(NTSTATUS LowerDeviceRemovalStatus) override
			{
				ASSERT(m_pBusDevice);

				if (WasReportedMissing())
				{
					RemoveThisFromBusDeviceList();
					//This object will be automatically deleted due to bDeleteThisAfterRemoveRequest set to true on creation
					return __super::OnRemoveDevice(LowerDeviceRemovalStatus);
				}
				else
				{
					DisableInterface();
					SetNewPNPState(NotStarted);
					return STATUS_SUCCESS;
				}
			}
		};

		typedef _BusPDOT<> BusPDO;

		template <class _PDO, class _Key = String, class _Compare = std::less<_Key>> class _GenericIndexedBus : public BusDevice
		{
		protected:
			typedef std::map<_Key, int, _Compare> _MappingType;

		private:
			_MappingType m_KeyToIDMapping;
			BazisLib::DDK::FastMutex m_MappingMutex;

		private:
			virtual void OnPDORequestedPlugout(PDOBase *pPDO) override
			{
				ASSERT(pPDO);
				int id = (int)pPDO->GetUniqueID();

				m_MappingMutex.Lock();
				for each(const _MappingType::value_type &kv in m_KeyToIDMapping)
				{
					if (kv.second == id)
					{
						_Key key = kv.first;
						m_MappingMutex.Unlock();
						DoEjectOrPlugoutPDO(key, false);
						return;
					}
				}
				m_MappingMutex.Unlock();
				return __super::OnPDORequestedPlugout(pPDO);
			}

			bool IsEjectAllKey(const TempString &key)
			{
				return key == L"*";
			}

			bool IsEjectAllKey(unsigned key)
			{
				return key == -1;
			}

			NTSTATUS DoEjectOrPlugoutPDO(const _Key &key, bool Eject)
			{
				if (IsEjectAllKey(key))
				{
					m_MappingMutex.Lock();
					_MappingType ids;
					ids.swap(m_KeyToIDMapping);
					m_MappingMutex.Unlock();

					for each(const _MappingType::value_type &pair in ids)
					{
						PDOBase *pPDO = __super::FindPDOByUniqueID(pair.second);
						if (!pPDO)
							continue;

						ShutdownPDO(static_cast<_PDO *>(pPDO));

						if (!Eject)
							pPDO->PlugOut();
						else
							pPDO->Eject();
					}
				}
				else
				{
					m_MappingMutex.Lock();
					_MappingType::iterator it = m_KeyToIDMapping.find(key);
					if (it == m_KeyToIDMapping.end())
					{
						m_MappingMutex.Unlock();
						return STATUS_NOT_FOUND;
					}
					unsigned id = it->second;
					m_MappingMutex.Unlock();
					PDOBase *pPDO = __super::FindPDOByUniqueID(id);
					if (!pPDO)
						return STATUS_NOT_FOUND;
					ShutdownPDO(static_cast<_PDO *>(pPDO));

					if (!Eject)
						pPDO->PlugOut();
					else
						pPDO->Eject();

					m_MappingMutex.Lock();
					m_KeyToIDMapping.erase(it);
					m_MappingMutex.Unlock();
				}
				return STATUS_SUCCESS;
			}

		protected:
			_GenericIndexedBus(String BusPrefix,
				bool bDeleteThisAfterRemoveRequest = false,
				ULONG DeviceCharacteristics = FILE_DEVICE_SECURE_OPEN,
				bool bExclusive = FALSE,
				ULONG AdditionalDeviceFlags = DO_POWER_PAGABLE)
				: BusDevice(BusPrefix, bDeleteThisAfterRemoveRequest, DeviceCharacteristics, bExclusive, AdditionalDeviceFlags)
			{

			}

			NTSTATUS InsertPDO(const _Key &Key, _PDO *pPDO, OUT LONG *pUniqueID = NULL)
			{
				LONG id = 0;

				{
					FastMutexLocker lck(m_MappingMutex);
					if (m_KeyToIDMapping.find(Key) != m_KeyToIDMapping.end())
						return STATUS_OBJECT_NAME_COLLISION;
					m_KeyToIDMapping[Key] = -1;
				}

				NTSTATUS status = AddPDO(pPDO, &id);
				if (!NT_SUCCESS(status))
				{
					FastMutexLocker lck(m_MappingMutex);
					m_KeyToIDMapping.erase(Key);
					return status;
				}

				FastMutexLocker lck(m_MappingMutex);
				m_KeyToIDMapping[Key] = id;
				if (pUniqueID)
					*pUniqueID = id;
				return STATUS_SUCCESS;
			}

			_PDO *FindPDO(const _Key &Key)
			{
				_MappingType::iterator it;
				{
					FastMutexLocker lck(m_MappingMutex);
					it = m_KeyToIDMapping.find(Key);
					if (it == m_KeyToIDMapping.end())
						return NULL;
				}
				PDOBase *pPDO = FindPDOByUniqueID(it->second);
				if (!pPDO)
					return NULL;
				return static_cast<_PDO *>(pPDO);
			}

			bool PDOExists(const _Key &key)
			{
				FastMutexLocker lck(m_MappingMutex);
				return (m_KeyToIDMapping.find(key) != m_KeyToIDMapping.end());
			}

			bool ReplacePDOKey(const _Key &oldKey, const _Key &newKey)
			{
				FastMutexLocker lck(m_MappingMutex);
				_MappingType::iterator it = m_KeyToIDMapping.find(oldKey);
				if (it == m_KeyToIDMapping.end())
					return false;

				int id = it->second;
				m_KeyToIDMapping.erase(it);
				m_KeyToIDMapping[newKey] = id;
				return true;
			}

			NTSTATUS EjectPDO(const _Key &Key) {return DoEjectOrPlugoutPDO(Key, true);}
			NTSTATUS PlugoutPDO(const _Key &Key) {return DoEjectOrPlugoutPDO(Key, false);}
			
			std::vector<_Key> ExportKeyList()
			{
				std::vector<_Key> result;
				result.reserve(m_KeyToIDMapping.size());
				FastMutexLocker lck(m_MappingMutex);
				for each(const _MappingType::value_type &pair in m_KeyToIDMapping)
					result.push_back(pair.first);
				return result;
			}

		protected:
			class SynchronizedMapPointer
			{
			private:
				_GenericIndexedBus &m_BusRef;

			public:
				SynchronizedMapPointer(_GenericIndexedBus &BusRef)
					: m_BusRef(BusRef)
				{
					m_BusRef.m_MappingMutex.Lock();
				}

				~SynchronizedMapPointer()
				{
					m_BusRef.m_MappingMutex.Unlock();
				}

				_MappingType *operator->()
				{
					return &m_BusRef.m_KeyToIDMapping;
				}
			};

		protected:
			//! Override this to perform some specific PDO cleanup before ejection/plugout
			virtual void ShutdownPDO(_PDO *pPDO) {}
		};
	}
}