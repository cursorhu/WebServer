/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft GPL 2.0
 */ 
#include "log.h"

using namespace std;

Log::Log() {
    lineCount_ = 0;
    isAsync_ = false;
    writeThread_ = nullptr;
    deque_ = nullptr;

    toDay_ = 0;
    fp_ = nullptr;
}

Log::~Log() {
    if(writeThread_ && writeThread_->joinable()) {
        while(!deque_->empty()) {
            deque_->flush();
        };
        deque_->Close();
        writeThread_->join();
    }
    if(fp_) {
        flush();
        fclose(fp_);
    }
}

int Log::GetLevel() const {
    return level_;
}

void Log::SetLevel(int level) {
    lock_guard<mutex> locker(mtx_);
    level_ = level;
}

void Log::init(int level = 1, const char* path, const char* suffix,
    int maxQueueSize) {
    isOpen_ = true;
    level_ = level;
    if(maxQueueSize > 0) {
        isAsync_ = true;
        if(!deque_) {
            unique_ptr<BlockDeque<std::string>> newDeque(new BlockDeque<std::string>);
            deque_ = move(newDeque);
            std::unique_ptr<std::thread> NewThread(new thread(FlushLogThread));
            writeThread_ = move(NewThread);
        }
    } else {
        isAsync_ = false;
    }
    buff_.RetrieveAll();
    lineCount_ = 0;

    time_t timer = time(nullptr);
    struct tm *sysTime = localtime(&timer);
    struct tm t = *sysTime;
    path_ = path;
    suffix_ = suffix;
    char fileName[LOG_NAME_LEN] = {0};
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", 
            path_, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, suffix_);
    toDay_ = t.tm_mday;
    
    if(fp_) { fclose(fp_); }
    fp_ = fopen(fileName, "a");
    if(fp_ == nullptr) {
        mkdir(path_, 0777);
        fp_ = fopen(fileName, "a");
    } 
    assert(fp_ != nullptr);
}

void Log::write(int level, const char *format, ...) {
    if(isAsync_) { deque_->flush(); } 

    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t tSec = now.tv_sec;
    struct tm *sysTime = localtime(&tSec);
    struct tm t = *sysTime;
    
    {
        unique_lock<mutex> locker(mtx_);
        locker.unlock();
        /* 日志日期 日志行数 */
        if (toDay_ != t.tm_mday || (lineCount_ && (lineCount_  %  MAX_LINES == 0)))
        {
            char newFile[LOG_NAME_LEN];
            char tail[16] = {0};
            sprintf(tail, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

            if (toDay_ != t.tm_mday)
            {
                snprintf(newFile, LOG_NAME_LEN - 1, "%s/%s%s", path_, tail, suffix_);
                toDay_ = t.tm_mday;
                lineCount_ = 0;
            }
            else {
                snprintf(newFile, LOG_NAME_LEN - 1, "%s/%s-%d%s",
                         path_, tail, (lineCount_  / MAX_LINES), suffix_);
            }

            locker.lock();
            fflush(fp_);
            fclose(fp_);
            fp_ = fopen(newFile, "a");
            assert(fp_ != nullptr);
        }
        lineCount_++;
    }

    va_list vaList;
    va_start(vaList, format);
    {
        lock_guard<mutex> locker(mtx_);
        int n = sprintf(buff_.BeginWrite(), "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
            t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);
        buff_.HasWritten(n);

        AppendLogLevel_();

        int m = vsprintf(buff_.BeginWrite(), format, vaList);
        buff_.HasWritten(m);

        buff_.Append("\n\0", 2);
    }

    if(isAsync_ || (deque_ && deque_->full())) {
         deque_->push_back(buff_.RetrieveAllToStr());
    } 
    else {
        lock_guard<mutex> locker(mtx_);
        fputs(buff_.Peek(), fp_);
        buff_.RetrieveAll();
    }
    va_end(vaList);
}

void Log::AppendLogLevel_() {
    switch(level_) {
    case 0:
        buff_.Append("[debug]: ", 9);
        break;
    case 1:
        buff_.Append("[info] : ", 9);
        break;
    case 2:
        buff_.Append("[warn] : ", 9);
        break;
    case 3:
        buff_.Append("[error]: ", 9);
        break;
    default:
        buff_.Append("[info] : ", 9);
        break;
    }
}

void Log::flush(void) {
    lock_guard<mutex> locker(mtx_);
    fflush(fp_);
}

void Log::AsyncWrite_() {
    string str = "";
    while(deque_->pop(str)) {
        lock_guard<mutex> locker(mtx_);
        fputs(str.c_str(), fp_);
    }
}

Log* Log::Instance() {
    static Log inst;
    return &inst;
}

void Log::FlushLogThread() {
    Log::Instance()->AsyncWrite_();
}