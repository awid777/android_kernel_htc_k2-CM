/*
 * arch/arm/kernel/topology.c
 *
 * Copyright (C) 2011 Linaro Limited.
 * Written by: Vincent Guittot
 *
 * based on arch/sh/kernel/topology.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/node.h>
#include <linux/nodemask.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/cputype.h>
#include <asm/topology.h>

static DEFINE_PER_CPU(unsigned long, cpu_scale);

unsigned long arch_scale_freq_power(struct sched_domain *sd, int cpu)
{
  return per_cpu(cpu_scale, cpu);
}

static void set_power_scale(unsigned int cpu, unsigned long power)
{
  per_cpu(cpu_scale, cpu) = power;
}

#ifdef CONFIG_OF
struct cpu_efficiency {
  const char *compatible;
  unsigned long efficiency;
};

struct cpu_efficiency table_efficiency[] = {
  {"arm,cortex-a15", 3891},
  {"arm,cortex-a7",  2048},
  {NULL, },
};

struct cpu_capacity {
  unsigned long hwid;
  unsigned long capacity;
};

struct cpu_capacity *cpu_capacity;

unsigned long middle_capacity = 1;

static void __init parse_dt_topology(void)
{
  struct cpu_efficiency *cpu_eff;
  struct device_node *cn = NULL;
  unsigned long min_capacity = (unsigned long)(-1);
  unsigned long max_capacity = 0;
  unsigned long capacity = 0;
  int alloc_size, cpu = 0;

  alloc_size = nr_cpu_ids * sizeof(struct cpu_capacity);
  cpu_capacity = (struct cpu_capacity *)kzalloc(alloc_size, GFP_NOWAIT);

  while ((cn = of_find_node_by_type(cn, "cpu"))) {
    const u32 *rate, *reg;
    int len;

    if (cpu >= num_possible_cpus())
      break;

    for (cpu_eff = table_efficiency; cpu_eff->compatible; cpu_eff++)
      if (of_device_is_compatible(cn, cpu_eff->compatible))
        break;

    if (cpu_eff->compatible == NULL)
      continue;

    rate = of_get_property(cn, "clock-frequency", &len);
    if (!rate || len != 4) {
      pr_err("%s missing clock-frequency property\n",
        cn->full_name);
      continue;
    }

    reg = of_get_property(cn, "reg", &len);
    if (!reg || len != 4) {
      pr_err("%s missing reg property\n", cn->full_name);
      continue;
    }

    capacity = ((be32_to_cpup(rate)) >> 20) * cpu_eff->efficiency;

    /* Save min capacity of the system */
    if (capacity < min_capacity)
      min_capacity = capacity;

    /* Save max capacity of the system */
    if (capacity > max_capacity)
      max_capacity = capacity;

    cpu_capacity[cpu].capacity = capacity;
    cpu_capacity[cpu++].hwid = be32_to_cpup(reg);
  }

  if (cpu < num_possible_cpus())
    cpu_capacity[cpu].hwid = (unsigned long)(-1);

  if (min_capacity == max_capacity)
    cpu_capacity[0].hwid = (unsigned long)(-1);
  else if (4*max_capacity < (3*(max_capacity + min_capacity)))
    middle_capacity = (min_capacity + max_capacity)
        >> (SCHED_POWER_SHIFT+1);
  else
    middle_capacity = ((max_capacity / 3)
        >> (SCHED_POWER_SHIFT-1)) + 1;

}

void update_cpu_power(unsigned int cpu, unsigned long hwid)
{
  unsigned int idx = 0;

  /* look for the cpu's hwid in the cpu capacity table */
  for (idx = 0; idx < num_possible_cpus(); idx++) {
    if (cpu_capacity[idx].hwid == hwid)
      break;

    if (cpu_capacity[idx].hwid == -1)
      return;
  }

  if (idx == num_possible_cpus())
    return;

  set_power_scale(cpu, cpu_capacity[idx].capacity / middle_capacity);

  printk(KERN_INFO "CPU%u: update cpu_power %lu\n",
    cpu, arch_scale_freq_power(NULL, cpu));
}

#else
static inline void parse_dt_topology(void) {}
static inline void update_cpu_power(unsigned int cpuid, unsigned int mpidr) {}
#endif 

#define MPIDR_SMP_BITMASK (0x3 << 30)
#define MPIDR_SMP_VALUE (0x2 << 30)

#define MPIDR_MT_BITMASK (0x1 << 24)

#define MPIDR_HWID_BITMASK 0xFFFFFF

#define MPIDR_LEVEL0_MASK 0x3
#define MPIDR_LEVEL0_SHIFT 0

#define MPIDR_LEVEL1_MASK 0xF
#define MPIDR_LEVEL1_SHIFT 8

#define MPIDR_LEVEL2_MASK 0xFF
#define MPIDR_LEVEL2_SHIFT 16

struct cputopo_arm cpu_topology[NR_CPUS];

const struct cpumask *cpu_coregroup_mask(int cpu)
{
	return &cpu_topology[cpu].core_sibling;
}

void update_siblings_masks(unsigned int cpuid)
{
  struct cputopo_arm *cpu_topo, *cpuid_topo = &cpu_topology[cpuid];
  int cpu;

  /* update core and thread sibling masks */
  for_each_possible_cpu(cpu) {
    cpu_topo = &cpu_topology[cpu];

  if (cpuid_topo->socket_id != cpu_topo->socket_id)
      continue;

  cpumask_set_cpu(cpuid, &cpu_topo->core_sibling);
    if (cpu != cpuid)
      cpumask_set_cpu(cpu, &cpuid_topo->core_sibling);

  if (cpuid_topo->core_id != cpu_topo->core_id)
      continue;

  cpumask_set_cpu(cpuid, &cpu_topo->thread_sibling);
    if (cpu != cpuid)
      cpumask_set_cpu(cpu, &cpuid_topo->thread_sibling);
  }
  smp_wmb();
}

void store_cpu_topology(unsigned int cpuid)
{
	struct cputopo_arm *cpuid_topo = &cpu_topology[cpuid];
	unsigned int mpidr;

	
	if (cpuid_topo->core_id != -1)
		return;

	mpidr = read_cpuid_mpidr();

	
	if ((mpidr & MPIDR_SMP_BITMASK) == MPIDR_SMP_VALUE) {

		if (mpidr & MPIDR_MT_BITMASK) {
			
			cpuid_topo->thread_id = (mpidr >> MPIDR_LEVEL0_SHIFT)
				& MPIDR_LEVEL0_MASK;
			cpuid_topo->core_id = (mpidr >> MPIDR_LEVEL1_SHIFT)
				& MPIDR_LEVEL1_MASK;
			cpuid_topo->socket_id = (mpidr >> MPIDR_LEVEL2_SHIFT)
				& MPIDR_LEVEL2_MASK;
		} else {
			
			cpuid_topo->thread_id = -1;
			cpuid_topo->core_id = (mpidr >> MPIDR_LEVEL0_SHIFT)
				& MPIDR_LEVEL0_MASK;
			cpuid_topo->socket_id = (mpidr >> MPIDR_LEVEL1_SHIFT)
				& MPIDR_LEVEL1_MASK;
		}
	} else {
		cpuid_topo->thread_id = -1;
		cpuid_topo->core_id = 0;
		cpuid_topo->socket_id = -1;
	}

  update_siblings_masks(cpuid);

  update_cpu_power(cpuid, mpidr & MPIDR_HWID_BITMASK);

	printk(KERN_INFO "CPU%u: thread %d, cpu %d, socket %d, mpidr %x\n",
		cpuid, cpu_topology[cpuid].thread_id,
		cpu_topology[cpuid].core_id,
		cpu_topology[cpuid].socket_id, mpidr);
}

#ifdef CONFIG_SCHED_HMP

static const char * const little_cores[] = {
  "arm,cortex-a7",
  NULL,
};

static bool is_little_cpu(struct device_node *cn)
{
  const char * const *lc;
  for (lc = little_cores; *lc; lc++)
    if (of_device_is_compatible(cn, *lc))
      return true;
  return false;
}

void __init arch_get_fast_and_slow_cpus(struct cpumask *fast,
          struct cpumask *slow)
{
  struct device_node *cn = NULL;
  int cpu = 0;

  cpumask_clear(fast);
  cpumask_clear(slow);

  if (strlen(CONFIG_HMP_FAST_CPU_MASK) && strlen(CONFIG_HMP_SLOW_CPU_MASK)) {
    if (cpulist_parse(CONFIG_HMP_FAST_CPU_MASK, fast))
      WARN(1, "Failed to parse HMP fast cpu mask!\n");
    if (cpulist_parse(CONFIG_HMP_SLOW_CPU_MASK, slow))
      WARN(1, "Failed to parse HMP slow cpu mask!\n");
    return;
  }


  while ((cn = of_find_node_by_type(cn, "cpu"))) {

    if (cpu >= num_possible_cpus())
      break;

    if (is_little_cpu(cn))
      cpumask_set_cpu(cpu, slow);
    else
      cpumask_set_cpu(cpu, fast);

    cpu++;
  }

  if (!cpumask_empty(fast) && !cpumask_empty(slow))
    return;

  cpumask_setall(fast);
  cpumask_clear(slow);
}

void __init arch_get_hmp_domains(struct list_head *hmp_domains_list)
{
  struct cpumask hmp_fast_cpu_mask;
  struct cpumask hmp_slow_cpu_mask;
  struct hmp_domain *domain;

  arch_get_fast_and_slow_cpus(&hmp_fast_cpu_mask, &hmp_slow_cpu_mask);

  if(!cpumask_empty(&hmp_slow_cpu_mask)) {
    domain = (struct hmp_domain *)
      kmalloc(sizeof(struct hmp_domain), GFP_KERNEL);
    cpumask_copy(&domain->cpus, &hmp_slow_cpu_mask);
    list_add(&domain->hmp_domains, hmp_domains_list);
  }
  domain = (struct hmp_domain *)
    kmalloc(sizeof(struct hmp_domain), GFP_KERNEL);
  cpumask_copy(&domain->cpus, &hmp_fast_cpu_mask);
  list_add(&domain->hmp_domains, hmp_domains_list);
}
#endif /* CONFIG_SCHED_HMP */

int cluster_to_logical_mask(unsigned int socket_id, cpumask_t *cluster_mask)
{
  int cpu;

  if (!cluster_mask)
    return -EINVAL;

  for_each_online_cpu(cpu)
    if (socket_id == topology_physical_package_id(cpu)) {
      cpumask_copy(cluster_mask, topology_core_cpumask(cpu));
      return 0;
    }

  return -EINVAL;
}

void __init init_cpu_topology(void) 

{
	unsigned int cpu;

	
	for_each_possible_cpu(cpu) {
		struct cputopo_arm *cpu_topo = &(cpu_topology[cpu]);

		cpu_topo->thread_id = -1;
		cpu_topo->core_id =  -1;
		cpu_topo->socket_id = -1;
		cpumask_clear(&cpu_topo->core_sibling);
		cpumask_clear(&cpu_topo->thread_sibling);

	set_power_scale(cpu, SCHED_POWER_SCALE);
	}
	smp_wmb();

	parse_dt_topology();
}
