#include <iostream>
#include <random>
#include <conio.h>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>


boost::log::sources::severity_logger<boost::log::trivial::severity_level> serevity_logger;
boost::asio::io_service                                                   io_service;

class server_connection : public boost::enable_shared_from_this<server_connection> , boost::noncopyable 
{
    server_connection(const std::string& uid_string) : socket_(io_service), started_(true), uid_string_(uid_string), timer_(io_service) {}
    
    void start(boost::asio::ip::tcp::endpoint ep)
    {
        std::random_device random_device;
    	mersenne_twister_ = std::mt19937(random_device());
    	kbhit_thread_     = boost::thread(boost::bind(&server_connection::work_stopper, shared_from_this()));
    	socket_.async_connect(ep, boost::bind(&server_connection::on_connect, shared_from_this(), boost::placeholders::_1));
    	kbhit_thread_.detach();
    }
    
    public:
        /*methods*/
        static boost::shared_ptr<server_connection> start(boost::asio::ip::tcp::endpoint ep, const std::string& username)
        {
            boost::shared_ptr<server_connection> new_connection(new server_connection(username));
            new_connection->start(ep);
            return new_connection;
        }
        
    private:
        /*methods*/
        void stop()
        {
        	if (!started()) return;
        	BOOST_LOG_SEV(serevity_logger, boost::log::trivial::info) << "client '" << uid_string_ << "' was stopped work";
        	started_ = false;
        	socket_.close();
        }
        
        bool started() const
        {
        	return started_;
        }
        
        void work_stopper()
        {
        	while (!_kbhit())
        		boost::this_thread::sleep_for(boost::chrono::milliseconds(work_stopper_interval_msecs_));
            char ret_val_getch = _getch();
        	BOOST_LOG_SEV(serevity_logger, boost::log::trivial::info) << "button has been pressed, initiating work stoppage";
        
            do_write("stop\n");
            boost::this_thread::sleep_for(boost::chrono::milliseconds(ethalon_time_interval_msecs_));
        	stop();
        }
        
        uint32_t get_numer_from_message(const std::string& msg) const
        {
            uint32_t ret_val = 0;
            if (msg.empty()) return ret_val;
            std::string num_str = msg;
            num_str.erase(num_str.begin(), num_str.begin() + 4);
            std::string::iterator it = std::remove(num_str.begin(), num_str.end(), '\n');
            num_str.erase(it, num_str.end());
            if (!num_str.empty()) ret_val = static_cast<uint32_t>(std::stoi(num_str));
            return ret_val;
        }
        
        void on_connect(const boost::system::error_code& err)
        {
            if (!err) do_write("login " + uid_string_ + "\n");
            else      stop();
        }
        
        void on_read(const boost::system::error_code& err, size_t bytes) 
        {
            if (err) stop();
            if (!started()) return;
        
            std::string msg(read_buffer_.data(), bytes);
        
            if      (msg.find("login ") == 0) on_login();
            else if (msg.find("num") == 0)
            {
                if (get_answer_from_message(msg) == "client_list_stopped")
                {
        			BOOST_LOG_SEV(serevity_logger, boost::log::trivial::info) << "server says to stop working";
        
        			do_write("stop\n");
        			boost::this_thread::sleep_for(boost::chrono::milliseconds(ethalon_time_interval_msecs_));
        
                    stop();
                    return;
                }
        
                BOOST_LOG_SEV(serevity_logger, boost::log::trivial::info) << "received a number from the server: '" << get_numer_from_message(msg) << "'";
                generate_number();
            }
            else std::cerr << "invalid incoming message: " << msg << std::endl;
        }
        
        void on_login()
        {
            BOOST_LOG_SEV(serevity_logger, boost::log::trivial::info) << "connected to the server successfully";
            generate_number();
        }
        
        std::string get_answer_from_message(const std::string& msg) const
        {
        	std::string ret_val;
        	std::istringstream in(msg);
        	in >> ret_val >> ret_val;
            return ret_val;
        }
        
        void do_number() 
        {
            if (!started()) return;
        
            uint32_t num = static_cast<uint32_t>(mersenne_twister_()) % static_cast<uint32_t>(1024);
            std::string num_str = "num " + std::to_string(num) + "\n";
        	BOOST_LOG_SEV(serevity_logger, boost::log::trivial::info) << "sending a number to the server: '" << get_numer_from_message(num_str) << "'";
            do_write(num_str);
        }
        
        void generate_number()
        {
            timer_.expires_from_now(boost::posix_time::millisec(generate_num_interval_msecs_));
            timer_.async_wait(boost::bind(&server_connection::do_number, shared_from_this()));
        }
        
        void do_read() 
        {
            async_read(socket_, 
                       boost::asio::buffer(read_buffer_),
                       boost::bind(&server_connection::read_completely, shared_from_this(), boost::placeholders::_1, boost::placeholders::_2),
                       boost::bind(&server_connection::on_read,         shared_from_this(), boost::placeholders::_1, boost::placeholders::_2));
        }
        
        void do_write(const std::string& msg)
        {
            if (!started()) return;
            std::copy(msg.begin(), msg.end(), write_buffer_.data());
            socket_.async_write_some(boost::asio::buffer(write_buffer_.data(), msg.size()), boost::bind(&server_connection::do_read, shared_from_this()));
        }
        
        bool read_completely(const boost::system::error_code& err, size_t bytes) const
        {
            if (err) return false;
            return std::find(read_buffer_.data(), read_buffer_.data() + bytes, '\n') < read_buffer_.data() + bytes;
        }
        
        /*data*/
        static const uint32_t               buffer_length_ = 1024;
        boost::array<char, buffer_length_>  read_buffer_;
        boost::array<char, buffer_length_>  write_buffer_;
        boost::asio::ip::tcp::socket        socket_;
        bool                                started_;
        std::string                         uid_string_;
        boost::asio::deadline_timer         timer_;
        boost::thread                       kbhit_thread_;
        std::mt19937                        mersenne_twister_;
        const uint32_t                      generate_num_interval_msecs_ = 1500;
        const uint32_t                      work_stopper_interval_msecs_ = 150;
        const uint32_t                      ethalon_time_interval_msecs_ = 900;
};

void init_boost_log(const std::string& uid)
{
boost::log::add_file_log
(
	boost::log::keywords::file_name           = "logs/async_client_uid_" + uid + "_log_%N.log",
	boost::log::keywords::rotation_size       = 10 * 1024 * 1024,
	boost::log::keywords::time_based_rotation = boost::log::sinks::file::rotation_at_time_point(0, 0, 0),
	boost::log::keywords::format              = "[%TimeStamp%]: %Message%"
);

boost::log::add_console_log
(
    std::cout, 
    boost::log::keywords::format = "[%TimeStamp%]: %Message%"
);

boost::log::core::get()->set_filter
(
	boost::log::trivial::severity >= boost::log::trivial::info
);
}

int main(int argc, char* argv[]) 
{
    boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), 8001);
    
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    std::string uid_string = { boost::lexical_cast<std::string>(uuid).c_str(), 5 };
    
    init_boost_log(uid_string);
    boost::log::add_common_attributes();

	BOOST_LOG_SEV(serevity_logger, boost::log::trivial::info) << "client start work with uid: " << uid_string;

    server_connection::start(ep, uid_string);
    boost::this_thread::sleep(boost::posix_time::millisec(100));
    
    io_service.run();
    
    system("pause");
    return 0;
}
