/*
    Copyright (c) 2007-2015 Contributors as noted in the AUTHORS file

    This file is part of libzmq, the ZeroMQ core engine in C++.

    libzmq is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    As a special exception, the Contributors give you permission to link
    this library with independent modules to produce an executable,
    regardless of the license terms of these independent modules, and to
    copy and distribute the resulting executable under terms of your choice,
    provided that you also meet, for each linked independent module, the
    terms and conditions of the license of that module. An independent
    module is a module which is not derived from or based on this library.
    If you modify this library, you must extend this exception to your
    version of the library.

    libzmq is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
    License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "decoder_allocators.hpp"

#include <cmath>

#include "msg.hpp"

zmq::shared_message_memory_allocator::shared_message_memory_allocator(size_t bufsize_):
        buf(NULL),
        bufsize( 0 ),
        max_size( bufsize_ ),
        msg_refcnt( NULL ),
        maxCounters( std::ceil( static_cast<double>(max_size) / static_cast<double>(msg_t::max_vsm_size)) )
{

}

zmq::shared_message_memory_allocator::shared_message_memory_allocator(size_t bufsize_, size_t maxMessages):
        buf(NULL),
        bufsize( 0 ),
        max_size( bufsize_ ),
        msg_refcnt( NULL ),
        maxCounters( maxMessages )
{

}

zmq::shared_message_memory_allocator::~shared_message_memory_allocator()
{
    deallocate();
}

unsigned char* zmq::shared_message_memory_allocator::allocate()
{
    if (buf)
    {
        // release reference count to couple lifetime to messages
        zmq::atomic_counter_t *c = reinterpret_cast<zmq::atomic_counter_t *>(buf);

        // if refcnt drops to 0, there are no message using the buffer
        // because either all messages have been closed or only vsm-messages
        // were created
        if (c->sub(1)) {
            // buffer is still in use as message data. "Release" it and create a new one
            // release pointer because we are going to create a new buffer
            release();
        }
    }

    // if buf != NULL it is not used by any message so we can re-use it for the next run
    if (!buf) {
        // allocate memory for reference counters together with reception buffer
        size_t const allocationsize = max_size + sizeof(zmq::atomic_counter_t) + maxCounters * sizeof(zmq::atomic_counter_t);

        buf = static_cast<unsigned char *>( malloc(allocationsize) );
        alloc_assert (buf);

        new(buf) atomic_counter_t(1);
    }
    else
    {
        // release reference count to couple lifetime to messages
        zmq::atomic_counter_t *c = reinterpret_cast<zmq::atomic_counter_t *>(buf);
        c->set(1);
    }

    bufsize = max_size;
    msg_refcnt = reinterpret_cast<zmq::atomic_counter_t*>( buf + sizeof(atomic_counter_t) + max_size );
    return buf + sizeof( zmq::atomic_counter_t);
}

void zmq::shared_message_memory_allocator::deallocate()
{
    std::free(buf);
    buf = NULL;
    bufsize = 0;
    msg_refcnt = NULL;
}

unsigned char* zmq::shared_message_memory_allocator::release()
{
    unsigned char* b = buf;
    buf = NULL;
    bufsize = 0;
    msg_refcnt = NULL;

    return b;
}

void zmq::shared_message_memory_allocator::inc_ref()
{
    (reinterpret_cast<zmq::atomic_counter_t*>(buf))->add(1);
}

void zmq::shared_message_memory_allocator::call_dec_ref(void*, void* hint) {
    zmq_assert( hint );
    unsigned char* buf = static_cast<unsigned char*>(hint);
    zmq::atomic_counter_t *c = reinterpret_cast<zmq::atomic_counter_t *>(buf);

    if (!c->sub(1)) {
        c->~atomic_counter_t();
        free(buf);
        buf = NULL;
    }
}


size_t zmq::shared_message_memory_allocator::size() const
{
    return bufsize;
}

unsigned char* zmq::shared_message_memory_allocator::data()
{
    return buf + sizeof(zmq::atomic_counter_t);
}