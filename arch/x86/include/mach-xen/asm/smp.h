#ifndef _ASM_X86_SMP_H
#define _ASM_X86_SMP_H
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>
#include <linux/init.h>
#include <asm/percpu.h>

/*
 * We need the APIC definitions automatically as part of 'smp.h'
 */
#ifdef CONFIG_X86_LOCAL_APIC
# include <asm/mpspec.h>
# include <asm/apic.h>
# ifdef CONFIG_X86_IO_APIC
#  include <asm/io_apic.h>
# endif
#endif
#include <linux/thread_info.h>
#include <asm/cpumask.h>

extern unsigned int num_processors;

#ifndef CONFIG_XEN
DECLARE_PER_CPU(cpumask_t, cpu_sibling_map);
DECLARE_PER_CPU(cpumask_t, cpu_core_map);
DECLARE_PER_CPU(u16, cpu_llc_id);
#endif
DECLARE_PER_CPU(int, cpu_number);

static inline const struct cpumask *cpu_sibling_mask(int cpu)
{
	return cpumask_of(cpu);
}

static inline const struct cpumask *cpu_core_mask(int cpu)
{
	return cpumask_of(cpu);
}

#ifndef CONFIG_XEN
DECLARE_EARLY_PER_CPU(u16, x86_cpu_to_apicid);
DECLARE_EARLY_PER_CPU(u16, x86_bios_cpu_apicid);
#endif

#ifdef CONFIG_SMP

#ifndef CONFIG_XEN

/* Static state in head.S used to set up a CPU */
extern struct {
	void *sp;
	unsigned short ss;
} stack_start;

struct smp_ops {
	void (*smp_prepare_boot_cpu)(void);
	void (*smp_prepare_cpus)(unsigned max_cpus);
	void (*smp_cpus_done)(unsigned max_cpus);

	void (*stop_other_cpus)(int wait);
	void (*smp_send_reschedule)(int cpu);

	int (*cpu_up)(unsigned cpu);
	int (*cpu_disable)(void);
	void (*cpu_die)(unsigned int cpu);
	void (*play_dead)(void);

	void (*send_call_func_ipi)(const struct cpumask *mask);
	void (*send_call_func_single_ipi)(int cpu);
};

/* Globals due to paravirt */
extern void set_cpu_sibling_map(int cpu);

extern struct smp_ops smp_ops;

static inline void smp_send_stop(void)
{
	smp_ops.stop_other_cpus(0);
}

static inline void stop_other_cpus(void)
{
	smp_ops.stop_other_cpus(1);
}

static inline void smp_prepare_boot_cpu(void)
{
	smp_ops.smp_prepare_boot_cpu();
}

static inline void smp_prepare_cpus(unsigned int max_cpus)
{
	smp_ops.smp_prepare_cpus(max_cpus);
}

static inline void smp_cpus_done(unsigned int max_cpus)
{
	smp_ops.smp_cpus_done(max_cpus);
}

static inline int __cpu_up(unsigned int cpu)
{
	return smp_ops.cpu_up(cpu);
}

static inline int __cpu_disable(void)
{
	return smp_ops.cpu_disable();
}

static inline void __cpu_die(unsigned int cpu)
{
	smp_ops.cpu_die(cpu);
}

static inline void play_dead(void)
{
	smp_ops.play_dead();
}

static inline void smp_send_reschedule(int cpu)
{
	smp_ops.smp_send_reschedule(cpu);
}

static inline void arch_send_call_function_single_ipi(int cpu)
{
	smp_ops.send_call_func_single_ipi(cpu);
}

static inline void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	smp_ops.send_call_func_ipi(mask);
}

void cpu_disable_common(void);
void native_smp_prepare_boot_cpu(void);
void native_smp_prepare_cpus(unsigned int max_cpus);
void native_smp_cpus_done(unsigned int max_cpus);
int native_cpu_up(unsigned int cpunum);
int native_cpu_disable(void);
void native_cpu_die(unsigned int cpu);
void native_play_dead(void);
void play_dead_common(void);
void wbinvd_on_cpu(int cpu);
int wbinvd_on_all_cpus(void);

void smp_store_cpu_info(int id);
#define cpu_physical_id(cpu)	per_cpu(x86_cpu_to_apicid, cpu)

#else /* CONFIG_XEN */

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);
void xen_stop_other_cpus(int wait);
void xen_smp_send_reschedule(int cpu);
void xen_send_call_func_ipi(const struct cpumask *mask);
void xen_send_call_func_single_ipi(int cpu);

static inline void smp_send_stop(void)
{
	xen_stop_other_cpus(0);
}

#define smp_send_reschedule	xen_smp_send_reschedule
#define arch_send_call_function_single_ipi	xen_send_call_func_single_ipi
#define arch_send_call_function_ipi_mask	xen_send_call_func_ipi

void play_dead(void);

#endif /* CONFIG_XEN */

/* We don't mark CPUs online until __cpu_up(), so we need another measure */
static inline int num_booting_cpus(void)
{
	return cpumask_weight(cpu_callout_mask);
}
#elif /* !CONFIG_SMP && */ !defined(CONFIG_XEN)
#define wbinvd_on_cpu(cpu)     wbinvd()
static inline int wbinvd_on_all_cpus(void)
{
	wbinvd();
	return 0;
}
#endif /* CONFIG_SMP */

#ifdef CONFIG_XEN
int wbinvd_on_all_cpus(void);
#endif

extern unsigned disabled_cpus __cpuinitdata;

#include <asm/smp-processor-id.h>

#if defined(CONFIG_X86_LOCAL_APIC) && !defined(CONFIG_XEN)

#ifndef CONFIG_X86_64
static inline int logical_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_LOGICAL_ID(apic_read(APIC_LDR));
}

#endif

extern int hard_smp_processor_id(void);

#else /* CONFIG_X86_LOCAL_APIC */

# ifndef CONFIG_SMP
#  define hard_smp_processor_id()	0
# endif

#endif /* CONFIG_X86_LOCAL_APIC */

#endif /* __ASSEMBLY__ */
#endif /* _ASM_X86_SMP_H */