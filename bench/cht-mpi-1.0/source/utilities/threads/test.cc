/* CHT-MPI, version 1.0. An MPI-based Chunks and Tasks library
 *                       implementation.
 * For copyright and license information, see below under "Copyright and
 * license".
 * 
 * Primary academic reference: 
 * Chunks and Tasks: A programming model for parallelization of dynamic
 * algorithms,
 * Emanuel H. Rubensson and Elias Rudberg,
 * Parallel Computing 00, 00 (2013),
 * <http://dx.doi.org/10.1016/j.parco.2013.09.006>
 * 
 * For further information about Chunks and Tasks, see
 * <http://www.chunks-and-tasks.org>.
 * 
 * === Copyright and license ===
 * 
 * Copyright (c) 2009-2014 Emanuel H. Rubensson and Elias Rudberg. All
 *                         rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer listed
 *   in this license in the documentation and/or other materials provided
 *   with the distribution.
 * 
 * - Neither the name of the copyright holders nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * 
 * The copyright holders provide no reassurances that the source code
 * provided does not infringe any patent, copyright, or any other
 * intellectual property rights of third parties. The copyright holders
 * disclaim any liability to any recipient for claims brought against
 * recipient by any third party for infringement of that parties
 * intellectual property rights.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <iostream>
#include "../cht_threads.h"
void* foo(void*) {
  std::cout << cht::Threads::get_ID_string() 
	    << "   "
	    << cht::Threads::get_ID_number() 
	    << std::endl;
  sleep(2);
  return NULL;
}
void* foogrp(void*) {
  cht::Threads::yield();

  std::cout << cht::Threads::get_ID_string() << "   "
	    << cht::Threads::get_ID_number() << " --- before sleep 1" << std::endl;
  sleep(2);
  std::cout << cht::Threads::get_ID_string() << "   "
	    << cht::Threads::get_ID_number() << " --- starting blocking" << std::endl;
  cht::Threads::startingBlockingOperation();  
  sleep(1);
  cht::Threads::blockingOperationFinished();
  std::cout << cht::Threads::get_ID_string() << "   "
	    << cht::Threads::get_ID_number() << " --- blocking op finished" << std::endl;
  return NULL;
}
int main() {
  {
    cht::Threads::ThreadGroup tg("groupOne",foogrp, NULL, 3, 1); 
    cht::Threads::Thread t1("tjoho 1", foo, NULL);
    cht::Threads::Thread t2("tjoho 2", foo, NULL);
    cht::Threads::ThreadGroup tg2("groupTwo",foo, NULL, 3, 1); 
    std::cout << "All threads created. " << std::endl;
  }
  std::cout << "After thread destruction. " << std::endl;
  
}