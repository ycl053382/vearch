// Copyright 2019 The Vearch Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.

package bufalloc

import (
	"sync"

	"github.com/vearch/vearch/internal/util/cbbytes"
)

const (
	baseSize = 15
	bigSize  = 64 * cbbytes.KB
)

var buffPool *bufferPool

func init() {
	buffPool = &bufferPool{
		baseline: [...]int{64, 128, 256, 512, cbbytes.KB, 2 * cbbytes.KB, 4 * cbbytes.KB, 8 * cbbytes.KB, 16 * cbbytes.KB, 32 * cbbytes.KB, 64 * cbbytes.KB, 128 * cbbytes.KB, 256 * cbbytes.KB, 512 * cbbytes.KB, cbbytes.MB},
	}
	for i, n := range buffPool.baseline {
		buffPool.pool[i] = createPool(n)
	}
	buffPool.pool[baseSize] = createPool(0)
}

func createPool(n int) *sync.Pool {
	return &sync.Pool{
		New: func() interface{} {
			if n == 0 || n > bigSize {
				return &ibuffer{}
			}
			return &ibuffer{buf: makeSlice(n)}
		},
	}
}

type bufferPool struct {
	baseline [baseSize]int
	pool     [baseSize + 1]*sync.Pool
}

func (p *bufferPool) getPoolNum(n int) int {
	for i, x := range p.baseline {
		if n <= x {
			return i
		}
	}
	return baseSize
}

func (p *bufferPool) getBuffer(n int) Buffer {
	num := p.getPoolNum(n)
	pool := p.pool[num]
	buf := pool.Get().(Buffer)
	if buf.Cap() < n {
		// return old buffer to pool
		buffPool.putBuffer(buf)
		buf = &ibuffer{buf: makeSlice(n)}
	}
	buf.Reset()
	return buf
}

func (p *bufferPool) putBuffer(buf Buffer) {
	num := p.getPoolNum(buf.Cap())
	pool := p.pool[num]
	pool.Put(buf)
}
