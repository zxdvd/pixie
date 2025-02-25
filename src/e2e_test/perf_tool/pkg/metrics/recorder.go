/*
 * Copyright 2018- The Pixie Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package metrics

import (
	"px.dev/pixie/src/e2e_test/perf_tool/experimentpb"
	"px.dev/pixie/src/e2e_test/perf_tool/pkg/pixie"
)

// Recorder is an interface for all metric recorders.
type Recorder interface {
	Start() error
	Close() error
}

// NewMetricsRecorder creates a new Recorder for the given MetricSpec.
// Currently, only supports PxL script recorders.
func NewMetricsRecorder(pxCtx *pixie.Context, spec *experimentpb.MetricSpec, resultCh chan<- *ResultRow) Recorder {
	switch spec.MetricType.(type) {
	case *experimentpb.MetricSpec_PxL:
		return &pxlScriptRecorderImpl{
			pxCtx: pxCtx,
			spec:  spec.GetPxL(),

			resultCh: resultCh,
		}
	}
	return nil
}
