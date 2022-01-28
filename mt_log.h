/** @addtogroup util
 * @{
 */

/** @file
 * 
 * @brief Simple multi-thread save logging system.
 *
 * @copyright
 *   (c) 2022 Drechsler Information Technologies, MIT License   
 *
 * @author
 *     - jrgdre Joerg.Drechsler@drechsler-it.de
 *
 * @version
 *     - 2022-01-28 2.0.0 jrgdre, add log level filter per stream feature
 *     - 2022-01-28 1.0.1 jrgdre, improve processing thread's idling
 *     - 2022-01-20 1.0.0 jrgdre, initial
 */

#ifndef MT_LOG_H
#define MT_LOG_H 

#include <iomanip>
#include <mutex>
#include <ostream>
#include <regex>
#include <string>
#include <thread>

/// Processing thread's sleep time in ms, if there is nothing to do.
#define MT_LOG_IDLE_SLEEP_MS 1

/**
 * @brief Severity levels of logged information.
 */
enum class MT_Log_Level {
    TRACE,   ///< Highest level of program execution details
    DEBUG,   ///< Details of program execution
    INFO,    ///< Informational message
    WARNING, ///< Possibly undesired condition detected
    ERROR,   ///< Error condition detected, but program was able to recover
    FATAL    ///< Unrecoverable error, program will terminate
};

/**
 * @brief Convert MT_Log_Level to string
 */
std::string mt_log_level_str(
    MT_Log_Level level ///< Level to convert
){
    switch(level){
        case MT_Log_Level::TRACE  : return "TRACE"  ; break;
        case MT_Log_Level::DEBUG  : return "DEBUG"  ; break;
        case MT_Log_Level::INFO   : return "INFO"   ; break;
        case MT_Log_Level::WARNING: return "WARNING"; break;
        case MT_Log_Level::ERROR  : return "ERROR"  ; break;
        case MT_Log_Level::FATAL  : return "FATAL"  ; break;
    }
}

/**
 * @brief Simple multi-tread save log system implementation.
 * 
 * @details
 *     The class implements a simple system that logs messages to registered
 *     output streams.
 * 
 *     The implementation is asynchronous and multi-thread save.
 * 
 * @note @parblock 
 *     - __Public__ methods are called in the context of the caller.
 *     - __Protected__ methods are called in the context of the internal message
 *      processing thread.
 * @endparblock
 * 
 */
class MT_Log {

private:
    /**
     * @brief Message queue's internal message representation.
     */
    struct Msg {
        std::string  src_id;  ///< Id of source logging the message
        std::string  text;    ///< Message's text
        MT_Log_Level level;   ///< Severity of message 
        uint64_t     time_ms; ///< Log time as milliseconds since epoch
    };

    /**
     * @brief Output streams list's item type.
     */
    struct Item {
        std::ostream * os;   ///< Stream to log to
        MT_Log_Level filter; ///< Don't log msgs below this level to this stream
    };

    std::mutex       m_msgs_mux;    ///< Mutex protecting msg queue access
    std::deque<Msg>  m_msgs;        ///< Queue of messages

    std::mutex       m_streams_mux; ///< Mutex protecting err set access
    std::list<Item>  m_streams;     ///< List of log streams

    std::mutex       m_term_mux;    ///< Mutex protecting log set access
    bool             m_terminate;   ///< Flag for thread termination
    std::thread      m_thread;      ///< Message processing thread

public:
    /**
     * @brief Construct a new MT_Log object
     */
    MT_Log(){
        m_terminate = false;
        m_thread    = std::thread(&MT_Log::process_msgs, this);
    }

    /**
     * @brief Destroy the MT_Log object
     */
    virtual ~MT_Log(){
        m_term_mux.lock();
        m_terminate = true;
        m_term_mux.unlock();
        m_thread.join();
    }

    /**
     * @brief Time function to use for time conversions.
     * 
     * Default is std::gmtime().
     */
    std::function<std::tm* (const std::time_t *time)> time_conv = std::gmtime;

    /**
     * @brief Provide current system time since epoch in milliseconds.
     */
    static uint64_t now() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    /**
     * @brief Add a message to the logs.
     * 
     * @param src_id    Id of source logging the message
     * @param text      Message's text
     * @param level     Severity of message 
     * @param time_ms   Log time as milliseconds since epoch
     */
    virtual void add(
        std::string  src_id,
        std::string  text,
        MT_Log_Level level   = MT_Log_Level::INFO,
        uint64_t     time_ms = MT_Log::now()
    ){
        // don't accept any new messages on termination
        m_term_mux.lock();
        bool term = m_terminate;
        m_term_mux.unlock();
        if (term)
            return;

        // add new message instance to end of the message queue
        std::lock_guard<std::mutex> guard(m_msgs_mux);
        struct Msg msg = {
            .src_id  = src_id,
            .text    = text,
            .level   = level,
            .time_ms = time_ms
        };
        m_msgs.push_back(std::move(msg));
    }

    /**
     * @brief Add a message to the logs.
     *        Use current thread's id as source id.
     * 
     * @param text      Message's text
     * @param level     Severity of message
     * @param time_ms   Log time as milliseconds since epoch
     */
    virtual void add(
        std::string     text,
        MT_Log_Level    level     = MT_Log_Level::INFO,        
        uint64_t        time_ms   = MT_Log::now()
    ){
        std::stringstream ss;
        ss << std::this_thread::get_id();
        add(ss.str(), text, level, time_ms);
    }

    /**
     * @brief Add a stream to the list of log streams.
     */
    virtual void add_stream(
        std::ostream * os,                       ///< Stream to add
        MT_Log_Level filter = MT_Log_Level::INFO ///< Don't log msgs below this level to this stream
    ){
        std::lock_guard<std::mutex> guard(m_streams_mux);
        Item item = {
            .os     = os,
            .filter = filter
        };
        m_streams.push_back(item);
    }

    /**
     * @brief Delete a stream from the list of log stream.
     * 
     * @note @parblock 
     *     If a reference to the same output stream was added more than once,
     *     all references are removed.
     * @endparblock
     */
    virtual void del_stream(
        std::ostream * os ///< Stream to delete
    ){
        std::lock_guard<std::mutex> guard(m_streams_mux);
        m_streams.remove_if(
            [os](Item item) {
                return item.os == os;
            }
        );
    };

protected:
    /**
     * @brief Format ms into a string using fmt.
     * 
     * In addition to the standard format specifiers of std::put_time() this
     * function accepts "%ms" as a format specifier for the position of the
     * three digit milliseconds part of the time.
     * 
     * https://stackoverflow.com/questions/22455357/milliseconds-since-epoch-to-dateformat-c
     * 
     * @see std::put_time()
     * @see time_conv
     */
    virtual std::string format_time(
        uint64_t    ms, ///< Time as milliseconds since epoch
        std::string fmt ///< String defining time format to write
    ){
        time_t tt = ms/1000;
        int    us = ms%1000;

        // Convert uint64_t to ISO time structure using conversion function in
        // parameter time_conv (std::localtime() or std::gmtime()[default])
        struct std::tm * ptm = time_conv(&tt);

        // Write number of µs as fixed three digit value into a stringstream.
        std::stringstream us_ss;
        us_ss << std::setfill('0') << std::setw(3) << us;

        // Replace "%ms" format specifier with value
        std::string fmtms = std::regex_replace(
            fmt, std::regex("%ms"), us_ss.str()
        );

        // Instantiate new string-stream and write time string to it.
        std::stringstream ss;
        ss << std::put_time(ptm, fmtms.c_str());

        return ss.str();
    }

    /**
     * @brief Thread function processing the masseges in the message queue.
     * 
     * @see write_msg()
     * 
     * @note @parblock 
     *     This method is called in the context of the internal message 
     *     processing thred.
     * @endparblock
     */
    virtual void process_msgs() {
        Msg  msg;
        bool term;

        m_term_mux.lock();
        term = m_terminate;
        m_term_mux.unlock();

        while (!term) {
            // write one message at a time
            m_msgs_mux.lock();
            if (!m_msgs.empty()) {
                msg = m_msgs.front();
                m_msgs.pop_front();
                m_msgs_mux.unlock();
                write_msg(msg);
            }
            else
                m_msgs_mux.unlock();
            // check termination flag
            m_term_mux.lock();
            term = m_terminate;
            m_term_mux.unlock();
            if (!term) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(MT_LOG_IDLE_SLEEP_MS)
                );
            };
        }
        // empty msg queue before terminating
        m_msgs_mux.lock();
        std::for_each (m_msgs.begin(), m_msgs.end(),
            [&, this](Msg msg) {
                write_msg(msg);
            };
        );
        m_msgs_mux.unlock();
    }

    /**
     * @brief Write a single message to the registered streams.
     * 
     * @see process_msgs()
     * @see write_msg_to_stream()
     * 
     * @note @parblock 
     *     This method is called in the context of the internal message 
     *     processing thred.
     * @endparblock
     * 
     */
    virtual void write_msg(
        Msg msg ///< Message to write to the streams
    ){
        auto ts = format_time(msg.time_ms, "%Y-%m-%e %H:%M:%S.%ms");
        auto ls = mt_log_level_str(msg.level);

        std::lock_guard<std::mutex> guard(m_streams_mux);

        if (m_streams.empty())
            return;

        std::for_each(m_streams.cbegin(), m_streams.cend(), 
            [&, this](Item item){
                if (msg.level >= item.filter)
                    write_msg_to_stream(item.os, ts, msg.src_id, ls, msg.text);
            }
        );
    }

    /**
     * @brief Write the message to the stream.
     * 
     * @see write_msg()
     * 
     * @note @parblock 
     *     This method is called in the context of the internal message 
     *     processing thred.
     * @endparblock
     */
    virtual void write_msg_to_stream(
        std::ostream * os,        ///< Stream to write message to
        std::string    time_str,  ///< Message's time as striing
        std::string    src_id,    ///< Message's source id as string
        std::string    level_str, ///< Message's severity as string
        std::string    text       ///< Message's text
    ){
        *os << time_str 
            << "\t" << src_id
            << "\t" << level_str
            << "\t" << text
            << std::endl;
    }
};

/**
 * @brief Default MT_LOG instance reference.
 * 
 * Since there is usually only one global logger instance, we declare it here,
 * so everyone can access it.
 * Of course the application still has to create the instance, before anyone
 * can use it.
 */
std::unique_ptr<MT_Log> mt_log;

#endif

/** @} */