#pragma once

#include <vector>
#include <optional>
#include <coroutine>
#include <exception>

#include <Windows.h>
#include "../handle_holder.hpp"
#include "../win32_error.hpp"

///
/// Called by signal_context.
///
struct signal_observer
{
    virtual ~signal_observer() = default;

    /// Checks if the signaled handle should not be treated as signaled.
    virtual bool is_spurious() const { return false; }

    /// Called when the handle becomes signaled and it's not spurious.
    virtual void on_signaled() = 0;
};

///
/// Waits for multiple handles to be signaled and invokes the associated observer.
///
class signal_context
{
public:
    signal_context() = default;
    signal_context(signal_context &&) = delete;

    /// Calls the observer inside the run() method when the handle is signaled and it's not spurious. Remove the installation.
    void install(HANDLE handle, signal_observer &o)
    {
        observers.emplace_back(&o);
        handles.emplace_back(handle);
    }

    /// Waits for all handles to be signaled and calls the observers. Observers may install new handles to be waited for.
    void run()
    {
        while(!handles.empty())
        {
            DWORD ret = WaitForMultipleObjects(handles.size(), handles.data(), FALSE, INFINITE);
            if(ret == WAIT_FAILED)
                throw win32_error(GetLastError(), "WaitForMultipleObjects failed.");

            size_t idx = ret - WAIT_OBJECT_0;
            if(idx < handles.size())
            {
                signal_observer *o = observers[idx];
                if(!o->is_spurious())
                {
                    handles.erase(handles.begin() + idx);
                    observers.erase(observers.begin() + idx);
                    o->on_signaled();
                }
            }
        }
    }

private:
    std::vector<HANDLE> handles;
    std::vector<signal_observer*> observers;
};


///
/// Coroutine awaiter which waits for a handle to be signaled.
///
class handle_awaiter : public signal_observer
{
public:
    handle_awaiter(signal_context &context, HANDLE h) : handle(h), context(&context) {}

    bool await_suspend(std::coroutine_handle<> coro) //called bevore suspension (co_await)
    {
        coroutine = std::move(coro);
        context->install(handle, *this);
        return true;
    }

    void await_resume() {} //called after co_await
    bool await_ready() const { return false; } //called to check whether to call suspend

    void on_signaled() override { coroutine.resume(); }

protected:
    HANDLE handle;
    signal_context *context;
    std::coroutine_handle<> coroutine;
};


///
/// Coroutine awaiter which waits for a thread to exit and returns the exit code.
/// 
class thread_awaiter : public handle_awaiter
{
public:
    using handle_awaiter::handle_awaiter;

    void on_signaled() override
    {
        if(!GetExitCodeThread(handle, &code))
            throw win32_error(GetLastError(), "GetExitCodeThread failed.");

        handle_awaiter::on_signaled();
    }

    DWORD await_resume() { return code; }

private:
    DWORD code = DWORD(-1);
};


///
/// Coroutine awaiter which waits for console keypress.
/// 
struct console_awaiter : public handle_awaiter
{
    console_awaiter(signal_context &ctx) : handle_awaiter(ctx, GetStdHandle(STD_INPUT_HANDLE)) {}

    bool is_spurious() const override
    {
        DWORD nevents = 0;
        GetNumberOfConsoleInputEvents(handle, &nevents);
        std::vector<INPUT_RECORD> inputs(nevents);
        ReadConsoleInputW(handle, inputs.data(), nevents, &nevents);
        for(const auto &rec : inputs)
        {
            if(rec.EventType == KEY_EVENT)
                return false;
        }
        return true;
    }
};


///
/// Lightweight class that represents a coroutine which does not promise any value.
/// Re-throws unhandled exceptions to the context in which it is called or resumed.
///
struct void_task
{
public:
    struct promise_type
    {
        void_task get_return_object() { return void_task(std::coroutine_handle<promise_type>::from_promise(*this)); }
        void unhandled_exception() { std::rethrow_exception(std::current_exception()); }
        void return_void() {}

        //suspend_always will cause the coroutine to not automatically be destructed, so it can be done in ~void_task
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
    };
    
    void_task() = default;
    void_task(std::coroutine_handle<> coroutine) noexcept : coroutine(std::move(coroutine)) {}

    void_task(const void_task &) = delete;
    void_task &operator=(const void_task&) = delete;

    void_task(void_task &&rhs) noexcept : coroutine(std::exchange(rhs.coroutine, nullptr)) {}
    void_task &operator=(void_task &&rhs) noexcept
    {
        if(coroutine)
            coroutine.destroy();
        coroutine = std::exchange(rhs.coroutine, nullptr);
        return *this;
    }

    //without suspend_always it would not be known whether the coroutine has already been destructed automatically
    ~void_task()
    {
        if(coroutine)
            coroutine.destroy();
    }

    //these methods make void_tasks themselves awaitable
    bool await_ready() { return coroutine.done(); }
    bool await_suspend(std::coroutine_handle<> coro)
    {
        if(!coroutine.done())
            coroutine.resume();
        return false;
    }

    void await_resume()
    {
        if(!coroutine.done())
            coroutine.resume();
    }

private:
    std::coroutine_handle<> coroutine;
};