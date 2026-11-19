/* User syscalls only: driver-internal context management and this worker's
 * kernel imports bypass these wrappers. Never inspect caller context memory. */
typedef int (*GeContextFn)(PspGeContext *);
typedef int (*GeBreakFn)(int, void *);

static GeContextFn real_ge_save, real_ge_restore;
static GeBreakFn real_ge_break;
static int ge_api_hooked;
static unsigned ge_api_calls[3];

static unsigned BeginGeApi(int index, int drain) {
    unsigned k1 = pspSdkSetK1(0);
    int intr = sceKernelCpuSuspendIntr();
    ++ge_api_entries;
    drain = drain && (test_profile & 1u);
    sceKernelCpuResumeIntr(intr);

    unsigned start = sceKernelGetSystemTimeLow();
    ++ge_api_calls[index];

    /* A context snapshot/restore requires GE idle. Let the worker retire its
     * owned list, while forbidding new submissions. Never cancel game work.
     * Break must run immediately: it may be needed to unstall the queue. */
    if (drain) {
        while (running && !reboot_exit && pending_list >= 0 &&
               (sceKernelGetSystemTimeLow() - start) < 100000u) {
            sceKernelDelayThread(250);
        }
    }
    pspSdkSetK1(k1);
    return k1;
}

static void EndGeApi(unsigned k1) {
    pspSdkSetK1(0);
    int intr = sceKernelCpuSuspendIntr();
    --ge_api_entries;
    sceKernelCpuResumeIntr(intr);
    pspSdkSetK1(k1);
}

static int GuardGeContext(PspGeContext *context, int restore) {
    GeContextFn original = restore ? real_ge_restore : real_ge_save;
    if (!running || reboot_exit || !reserved) return original(context);
    unsigned k1 = BeginGeApi(restore, 1);
    int result = original(context);
    EndGeApi(k1);
    return result;
}

static int GeSaveHook(PspGeContext *context) {
    return GuardGeContext(context, 0);
}

static int GeRestoreHook(PspGeContext *context) {
    return GuardGeContext(context, 1);
}

static int GeBreakHook(int mode, void *argument) {
    if (!running || reboot_exit || !reserved) return real_ge_break(mode, argument);
    unsigned k1 = BeginGeApi(2, 0);
    int result = real_ge_break(mode, argument);
    EndGeApi(k1);
    return result;
}

static void InstallGeApiHooks(void) {
    real_ge_save = (GeContextFn)(uintptr_t)Find("sceGE_Manager", "sceGe_user", 0x438a385a);
    real_ge_restore = (GeContextFn)(uintptr_t)Find("sceGE_Manager", "sceGe_user", 0x0bf608fb);
    real_ge_break = (GeBreakFn)(uintptr_t)Find("sceGE_Manager", "sceGe_user", 0xb448ec0d);
    /* User-library NIDs above match the inspected driver export table. */
    if (!real_ge_save || !real_ge_restore || !real_ge_break) return;
    sctrlHENPatchSyscall((void *)real_ge_save, GeSaveHook);
    sctrlHENPatchSyscall((void *)real_ge_restore, GeRestoreHook);
    sctrlHENPatchSyscall((void *)real_ge_break, GeBreakHook);
    ge_api_hooked = 1;
}

static void RemoveGeApiHooks(void) {
    if (!ge_api_hooked) return;
    sctrlHENPatchSyscall(GeSaveHook, (void *)real_ge_save);
    sctrlHENPatchSyscall(GeRestoreHook, (void *)real_ge_restore);
    sctrlHENPatchSyscall(GeBreakHook, (void *)real_ge_break);
    ge_api_hooked = 0;
}
