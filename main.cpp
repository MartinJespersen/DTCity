#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

#ifdef __GNUC__
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <complex.h>
#include <sys/inotify.h>
#include <sys/prctl.h>
#include <sys/signal.h>
#endif

// user defined: [hpp]
#include "base/base.hpp"
#include "entrypoint.hpp"

Context g_ctx_main;

void (*drawFrameLib)();
void (*InitContextLib)();
void (*DeleteContextLib)();
void (*GlobalContextSetLib)(Context*);

#ifdef __GNUC__

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))
#define CONCAT(a, b) a b
#define MAIN_LIB "./build/debug/"
#define MAIN_LIB_FILE "entrypoint.so"

const char* mainLibFile = CONCAT(MAIN_LIB, MAIN_LIB_FILE);
const char* sourceDir = "./";
const char* buildScript = "./build.sh";
int fd;
bool libChanged = false;
u8 notifyBuffer[BUF_LEN] __attribute__((aligned(__alignof__(struct inotify_event))));

void
setupChildDeathSignal()
{
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1)
    {
        std::cerr << "prctl failed: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }

    // Method 2: Check if parent died during prctl setup
    if (getppid() == 1)
    {
        std::cerr << "Parent died before prctl setup" << std::endl;
        exit(EXIT_FAILURE);
    }
}

// NOTE: A bug in system might cause the read function to fail in debug mode for some reason
void
listenForFileChanges(const char* srcDir)
{
    printf("Listening for file changes in %s\n", srcDir);
    int srcDirFd = {0};
    int srcDirWd = {0};
    u8 buffer[BUF_LEN] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len;

    srcDirFd = inotify_init();
    if (srcDirFd < 0)
    {
        printf("Failed to initialize inotify: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    srcDirWd = inotify_add_watch(srcDirFd, srcDir, IN_CLOSE_WRITE);
    if (srcDirWd < 0)
    {
        printf("Failed to add watch to inotify: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    while ((len = read(srcDirFd, buffer, BUF_LEN)) > 0)
    {
        u8* buf = &buffer[0];
        struct inotify_event* event;
        while (buf < buffer + len)
        {
            event = (struct inotify_event*)buf;
            if (event->mask & IN_CLOSE_WRITE)
            {
                int procRes = system(buildScript);
                printf("Process id is: %d", getpid());
                if (procRes == EXIT_FAILURE)
                {
                    printf("Failed to run build script: %s", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }
            buf += sizeof(struct inotify_event) + event->len;
        }
    }
}

void*
loadLibrary()
{
    void* entryHandle = dlopen(mainLibFile, RTLD_NOW | RTLD_LOCAL);
    if (!entryHandle)
    {
        printf("Failed to load entrypoint.so: %s", dlerror());
        exit(EXIT_FAILURE);
    }

    VulkanInitLib = (void (*)(Context*))dlsym(entryHandle, "VulkanInit");
    if (!VulkanInitLib)
    {
        printf("Failed to load VulkanInit: %s", dlerror());
        exit(EXIT_FAILURE);
    }

    drawFrameLib = (void (*)())dlsym(entryHandle, "drawFrame");
    if (!drawFrameLib)
    {
        printf("Failed to load drawFrame: %s", dlerror());
        exit(EXIT_FAILURE);
    }

    DeleteContextLib = (void (*)(Context*))dlsym(entryHandle, "DeleteContext");
    if (!DeleteContextLib)
    {
        printf("Failed to load DeleteContext function: %s", dlerror());
        exit(EXIT_FAILURE);
    }

    cleanupLib = (void (*)(Context*))dlsym(entryHandle, "cleanup");
    if (!cleanupLib)
    {
        printf("Failed to load cleanup: %s", dlerror());
        exit(EXIT_FAILURE);
    }

    {
        InitContextLib = (void (*)(Context*))dlsym(entryHandle, "InitContext");
        if (!InitContextLib)
        {
            printf("Failed to load InitContext: %s", dlerror());
            exit(EXIT_FAILURE);
        }
    }

    ThreadContextInitLib = (void (*)(Context*))dlsym(entryHandle, "ThreadContextInit");
    if (!ThreadContextInitLib)
    {
        printf("Failed to load ThreadContextInit: %s", dlerror());
        exit(EXIT_FAILURE);
    }

    ThreadContextExitLib = (void (*)(Context*))dlsym(entryHandle, "ThreadContextExit");
    if (!ThreadContextExitLib)
    {
        printf("Failed to load ThreadContextExit: %s", dlerror());
        exit(EXIT_FAILURE);
    }

    ThreadContextSetLib = (void (*)(ThreadCtx*))dlsym(entryHandle, "ThreadCxtSet");
    if (!ThreadContextSetLib)
    {
        printf("Failed to load ThreadCxtSet: %s", dlerror());
        exit(EXIT_FAILURE);
    }

    char* err = dlerror();
    if (err != NULL)
    {
        printf("%s", err);
        exit(EXIT_FAILURE);
    }

    return entryHandle;
}

// NOTE: This handler cannot be debugged with gdb when
void
signal_handler(int signo, siginfo_t* info, void* context)
{
    (void)signo;
    (void)info;
    (void)context;

    ssize_t len;
    while ((len = read(fd, notifyBuffer, BUF_LEN)) > 0)
    {
        u8* ptr = notifyBuffer;
        while (ptr < notifyBuffer + len)
        {
            struct inotify_event* event = (struct inotify_event*)ptr;
            if (event->len > 0)
            {
                if (event->mask & IN_CLOSE_WRITE)
                {
                    libChanged = true;
                }

                fflush(stdout);
            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
    }
}
#endif






void
run()
{
    ThreadCtx thread_ctx = {0};
#ifndef __GNUC__
#define PROFILING_ENABLE
#endif
#ifndef PROFILING_ENABLE 
    void* entryHandle = loadLibrary();
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;

    if (sigaction(SIGIO, &sa, NULL) == -1)
    {
        perror("sigaction SIGIO");
        exit(EXIT_FAILURE);
    }

    fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0)
    {
        printf("Failed to initialize inotify: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (fcntl(fd, F_SETOWN, getpid()) == -1)
    {
        perror("fcntl F_SETOWN");
        exit(EXIT_FAILURE);
    }

    if (fcntl(fd, F_SETFL, O_ASYNC | O_NONBLOCK) == -1)
    {
        perror("fcntl F_SETFL");
        exit(EXIT_FAILURE);
    }
    int wd = inotify_add_watch(fd, MAIN_LIB,
                               IN_CLOSE_WRITE); // IN_CLOSE_WRITE instead of IN_MODIFY or IN_CREATE
                                                // as the file might not be fully written to disk
    if (wd < 0)
    {
        printf("Failed to add watch to inotify: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int childPid = fork();
    switch (childPid)
    {
    case -1:
        perror("fork");
        exit(EXIT_FAILURE);
    case 0:
        setupChildDeathSignal();
        listenForFileChanges(sourceDir);
        break;
    default:
        setpgid(childPid, childPid);
        break;
    }

#else
    drawFrameLib = drawFrame;
    InitContextLib = InitContext;
    DeleteContextLib = DeleteContext;
    GlobalContextSetLib = GlobalContextSet;

#endif


    VulkanContext vulkanContext = {};
    ProfilingContext profilingContext = {};
    GlyphAtlas glyphAtlas = {};
    BoxContext rect = {};
    UI_IO input = {};
    UI_State ui_state = {};
    UI_Widget g_ui_widget = {&g_ui_widget, &g_ui_widget,&g_ui_widget,&g_ui_widget,&g_ui_widget,&g_ui_widget,&g_ui_widget,0};
    g_ctx_main = {
        &vulkanContext, &profilingContext, &glyphAtlas, &rect, &input, &ui_state, &thread_ctx, 0, 0, 0, &g_ui_widget};

    GlobalContextSetLib(&g_ctx_main);
    InitContextLib();

    while (!glfwWindowShouldClose(vulkanContext.window))
    {
        glfwPollEvents();

        glfwGetCursorPos(vulkanContext.window, &input.mousePosition.x, &input.mousePosition.y);
        input.leftClicked =
            glfwGetMouseButton(vulkanContext.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

#ifndef PROFILING_ENABLE

        if (libChanged)
        {
            libChanged = false;
            if (dlclose(entryHandle))
            {
                printf("Failed to close entrypoint.so: %s", dlerror());
                exit(EXIT_FAILURE);
            };

            entryHandle = nullptr;

            entryHandle = loadLibrary();
        }

#endif
        drawFrameLib();
    }
#ifndef PROFILING_ENABLE
    inotify_rm_watch(fd, wd);
#endif
    DeleteContextLib();
}

// NOTE: Tracy profiler has a dlclose function and it takes precedence over the one in the standard
// library
//  This is only the case when -DTRACY_ENABLE is defined
int
main()
{
    run();
    return EXIT_SUCCESS;
}
