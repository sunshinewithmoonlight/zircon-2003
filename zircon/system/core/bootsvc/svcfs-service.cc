// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svcfs-service.h"

#include <fuchsia/boot/c/fidl.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/job.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls/log.h>

#include "util.h"

namespace {

zx_status_t FactoryItemsGet(void* ctx, uint32_t extra, fidl_txn_t* txn) {
  auto map = static_cast<bootsvc::FactoryItemMap*>(ctx);
  auto it = map->find(extra);
  if (it == map->end()) {
    return fuchsia_boot_FactoryItemsGet_reply(txn, ZX_HANDLE_INVALID, 0);
  }

  const zx::vmo& vmo = it->second.vmo;
  uint32_t length = it->second.length;
  zx::vmo payload;
  zx_status_t status =
      vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & ~(ZX_RIGHT_WRITE | ZX_RIGHT_SET_PROPERTY), &payload);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to duplicate handle for factory item VMO: %s",
           zx_status_get_string(status));
    return status;
  }

  return fuchsia_boot_FactoryItemsGet_reply(txn, payload.release(), length);
}

constexpr fuchsia_boot_FactoryItems_ops kFactoryItemsOps = {
    .Get = FactoryItemsGet,
};

struct ItemsData {
  zx::vmo vmo;
  bootsvc::ItemMap map;
};

zx_status_t ItemsGet(void* ctx, uint32_t type, uint32_t extra, fidl_txn_t* txn) {
  auto data = static_cast<const ItemsData*>(ctx);
  auto it = data->map.find(bootsvc::ItemKey{type, extra});
  if (it == data->map.end()) {
    return fuchsia_boot_ItemsGet_reply(txn, ZX_HANDLE_INVALID, 0);
  }
  auto& item = it->second;
  auto buf = std::make_unique<uint8_t[]>(item.length);
  zx_status_t status = data->vmo.read(buf.get(), item.offset, item.length);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to read from boot image VMO: %s\n", zx_status_get_string(status));
    return status;
  }
  zx::vmo payload;
  status = zx::vmo::create(item.length, 0, &payload);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to create payload VMO: %s\n", zx_status_get_string(status));
    return status;
  }
  status = payload.write(buf.get(), 0, item.length);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to write to payload VMO: %s\n", zx_status_get_string(status));
    return status;
  }
  return fuchsia_boot_ItemsGet_reply(txn, payload.release(), item.length);
}

constexpr fuchsia_boot_Items_ops kItemsOps = {
    .Get = ItemsGet,
};

}  // namespace

namespace bootsvc {

fbl::RefPtr<SvcfsService> SvcfsService::Create(async_dispatcher_t* dispatcher) {
  return fbl::AdoptRef(new SvcfsService(dispatcher));
}

SvcfsService::SvcfsService(async_dispatcher_t* dispatcher)
    : vfs_(dispatcher), root_(fbl::MakeRefCounted<fs::PseudoDir>()) {}

void SvcfsService::AddService(const char* service_name, fbl::RefPtr<fs::Service> service) {
  root_->AddEntry(service_name, std::move(service));
}

zx_status_t SvcfsService::CreateRootConnection(zx::channel* out) {
  return CreateVnodeConnection(&vfs_, root_, fs::Rights::ReadWrite(), out);
}

fbl::RefPtr<fs::Service> CreateFactoryItemsService(async_dispatcher_t* dispatcher,
                                                   FactoryItemMap map) {
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, map = std::move(map)](zx::channel channel) mutable {
        auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_FactoryItems_dispatch);
        return fidl_bind(dispatcher, channel.release(), dispatch, &map, &kFactoryItemsOps);
      });
}

fbl::RefPtr<fs::Service> CreateItemsService(async_dispatcher_t* dispatcher, zx::vmo vmo,
                                            ItemMap map) {
  ItemsData data{std::move(vmo), std::move(map)};
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, data = std::move(data)](zx::channel channel) mutable {
        auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Items_dispatch);
        return fidl_bind(dispatcher, channel.release(), dispatch, &data, &kItemsOps);
      });
}

fbl::RefPtr<fs::Service> KernelStatsImpl::CreateService(async_dispatcher_t* dispatcher) {
  return fbl::MakeRefCounted<fs::Service>([dispatcher, this](zx::channel channel) mutable {
    return fidl::Bind(dispatcher, std::move(channel), this);
  });
}

void KernelStatsImpl::GetMemoryStats(GetMemoryStatsCompleter::Sync completer) {
  zx_info_kmem_stats_t mem_stats;
  zx_status_t status = root_resource_.get_info(ZX_INFO_KMEM_STATS, &mem_stats,
                                               sizeof(zx_info_kmem_stats_t), nullptr, nullptr);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  llcpp::fuchsia::kernel::MemoryStats::UnownedBuilder builder;
  builder.set_total_bytes(fidl::unowned(&mem_stats.total_bytes));
  builder.set_free_bytes(fidl::unowned(&mem_stats.free_bytes));
  builder.set_wired_bytes(fidl::unowned(&mem_stats.wired_bytes));
  builder.set_total_heap_bytes(fidl::unowned(&mem_stats.total_heap_bytes));
  builder.set_free_heap_bytes(fidl::unowned(&mem_stats.free_heap_bytes));
  builder.set_vmo_bytes(fidl::unowned(&mem_stats.vmo_bytes));
  builder.set_mmu_overhead_bytes(fidl::unowned(&mem_stats.mmu_overhead_bytes));
  builder.set_ipc_bytes(fidl::unowned(&mem_stats.ipc_bytes));
  builder.set_other_bytes(fidl::unowned(&mem_stats.other_bytes));
  completer.Reply(builder.build());
}

void KernelStatsImpl::GetCpuStats(GetCpuStatsCompleter::Sync completer) {
  zx_info_cpu_stats_t cpu_stats[ZX_CPU_SET_MAX_CPUS];
  size_t actual, available;
  zx_status_t status = root_resource_.get_info(ZX_INFO_CPU_STATS, cpu_stats,
                                               sizeof(zx_info_cpu_stats_t) * ZX_CPU_SET_MAX_CPUS,
                                               &actual, &available);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }

  llcpp::fuchsia::kernel::CpuStats stats;
  stats.actual_num_cpus = actual;
  llcpp::fuchsia::kernel::PerCpuStats per_cpu_stats[available];
  fbl::Vector<std::unique_ptr<llcpp::fuchsia::kernel::PerCpuStats::UnownedBuilder>> builders;
  stats.per_cpu_stats = fidl::VectorView(per_cpu_stats, available);
  for (uint32_t cpu_num = 0; cpu_num < available; ++cpu_num) {
    // TODO(fxb/42059) Switch to using owned heap allocated builders.
    builders.push_back(std::make_unique<llcpp::fuchsia::kernel::PerCpuStats::UnownedBuilder>());
    auto& builder = builders[cpu_num];
    auto& cpu_stat = cpu_stats[cpu_num];
    builder->set_cpu_number(fidl::unowned(&cpu_stat.cpu_number));
    builder->set_flags(fidl::unowned(&cpu_stat.flags));
    builder->set_idle_time(fidl::unowned(&cpu_stat.idle_time));
    builder->set_reschedules(fidl::unowned(&cpu_stat.reschedules));
    builder->set_context_switches(fidl::unowned(&cpu_stat.context_switches));
    builder->set_irq_preempts(fidl::unowned(&cpu_stat.irq_preempts));
    builder->set_yields(fidl::unowned(&cpu_stat.yields));
    builder->set_ints(fidl::unowned(&cpu_stat.ints));
    builder->set_timer_ints(fidl::unowned(&cpu_stat.timer_ints));
    builder->set_timers(fidl::unowned(&cpu_stat.timers));
    builder->set_page_faults(fidl::unowned(&cpu_stat.page_faults));
    builder->set_exceptions(fidl::unowned(&cpu_stat.exceptions));
    builder->set_syscalls(fidl::unowned(&cpu_stat.syscalls));
    builder->set_reschedule_ipis(fidl::unowned(&cpu_stat.reschedule_ipis));
    builder->set_generic_ipis(fidl::unowned(&cpu_stat.generic_ipis));
    per_cpu_stats[cpu_num] = builder->build();
  }
  completer.Reply(std::move(stats));
}

}  // namespace bootsvc
