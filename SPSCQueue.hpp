#include <vector>

namespace core {
#define CACHE_LINE_SIZE 64

    template<class T>
    class SPSCQueue
    {
        struct alignas (CACHE_LINE_SIZE) Node
        {
            T* m_data;
            Node* m_next;
            volatile bool m_ready;
            Node(): m_data(nullptr), m_next(nullptr), m_ready(false) {}
        };

        size_t m_size;
        int m_busytries;
        size_t m_num_grows;
        bool m_autogrow;
        Node* m_head;
        Node* m_write;
        Node* m_read;
    private:
        void write(T* obj)
        {
            m_write = m_write->m_next;
            m_write->m_data = obj;
        }

        void writeDone()
        {
            m_write->m_ready = true;
        }

        void grow()
        {
            Node* p = m_write->m_next;
            Node* newData = new Node[m_size];
            m_write->m_next = newData;
            for (size_t i = 0; i < m_size-1; ++i)
            {
                newData[i].m_next = &newData[i+1];
            }
            newData[m_size-1].m_next = p;
            m_size += m_size;
            ++m_num_grows;
        }

        void read(T*& obj)
        {
            m_read = m_read->m_next;
            obj = m_read->m_data;
        }

        void readDone()
        {
            m_read->m_ready = false;
        }

        static constexpr size_t DEFAULT_SIZE = 64;
    public:

        SPSCQueue(size_t size=DEFAULT_SIZE, bool autogrow=true, int busytries=100) : m_size(size), m_busytries(busytries), m_num_grows(0), m_autogrow(autogrow)
        {
            m_head = new Node[m_size];
            m_write = m_head;
            for (size_t i = 0; i < m_size-1; ++i)
            {
                m_write->m_next = &m_head[i+1];
                m_write = m_write->m_next;
            }

            // link back to the head
            m_write->m_next = m_head;
            m_read = m_head;
            m_write = m_head;
        }

        ~SPSCQueue()
        {
            delete[] m_head;
        }

        SPSCQueue(const SPSCQueue&) = delete;
        SPSCQueue(SPSCQueue&&) = delete;
        SPSCQueue& operator=(const SPSCQueue&) = delete;
        SPSCQueue& operator=(SPSCQueue&&) = delete;

        bool push(T* obj)
        {
            if(m_write->m_next == m_read)
            {
                if(m_autogrow)
                {
                    grow();
                }
                else
                {
                    return false;
                }
            }

            write(obj);
            writeDone();
            return true;
        }

        bool pop (T*& obj)
        {
            int i = 0;
            while(false == m_read->m_next->m_ready)
            {
                if(m_busytries < 0)
                {
                    continue; // busy waiting
                }
                else if(m_busytries > 1)
                {
                    ++i;
                }
                else // = 0, no wait
                {
                    return false;
                }
            }
            read(obj);
            readDone();
            return true;
        }

        size_t size() const { return m_size; }

        // this function is non-intrusive, should only be called from a different thread (different than writer/reader thread)
        size_t pending() const
        {
            // keep in mind this is just an approximation... not very accurate
            size_t c = 0;
            auto ptr = m_read;
            while(ptr != m_write)
            {
                ptr = ptr->m_next;
                ++c;
            }
            return c;
        }

        size_t num_grows() const { return m_num_grows; }
    };
} /* namespace core */
