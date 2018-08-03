/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{ApiMsg, AsyncBlobImageRasterizer, BlobImageParams, BlobImageRequest};
use api::{BlobImageResult, DocumentId, TransactionMsg};
use api::channel::MsgSender;
use renderer::ThreadListener;
use thread_profiler::register_thread_with_profiler;
use std::io;
use std::sync::Arc;
use std::sync::mpsc::{channel, Receiver, Sender};
use std::thread;

// Message from render backend to rasterizer.
pub enum RasterizationRequest {
    Transaction {
        blob_rasterizer: Option<Box<AsyncBlobImageRasterizer>>,
        blob_requests: Vec<BlobImageParams>,
        document_id: DocumentId,
        transaction_msg: TransactionMsg,
    },
    Stop
}

pub enum RasterizationResult {
    Transaction {
        blob_rasterizer: Option<Box<AsyncBlobImageRasterizer>>,
        rasterized_blobs: Vec<(BlobImageRequest, BlobImageResult)>,
        document_id: DocumentId,
        transaction_msg: TransactionMsg,
    },
}

pub struct Rasterizer {
    rx: Receiver<RasterizationRequest>,
    tx: Sender<RasterizationResult>,
    api_tx: MsgSender<ApiMsg>,
    name: String,
}

impl Rasterizer {
    pub fn create(
        name: String,
        thread_listener: Arc<Option<Box<ThreadListener + Send + Sync>>>,
        result_tx: Sender<RasterizationResult>,
        api_tx: MsgSender<ApiMsg>,
    ) -> io::Result<Sender<RasterizationRequest>> {
        let (tx, rx) = channel();
        let mut rasterizer = Rasterizer {
            rx,
            tx: result_tx,
            api_tx,
            name: name.clone()
        };

        thread::Builder::new().name(name.clone()).spawn(move || {
            register_thread_with_profiler(name.clone());
            if let Some(ref thread_listener) = *thread_listener {
                thread_listener.thread_started(&name);
            }

            rasterizer.run();

            if let Some(ref thread_listener) = *thread_listener {
                thread_listener.thread_stopped(&name);
            }
        })?;

        Ok(tx)
    }

    fn run(&mut self) {
        loop {
            match self.rx.recv() {
                Ok(msg) =>  {
                    if !self.process_message(msg) {
                        break;
                    }
                }
                Err(_) => {
                    break;
                }
            }
        }
    }

    fn process_message(&mut self, msg: RasterizationRequest) -> bool {
        match msg {
            RasterizationRequest::Transaction {
                mut blob_rasterizer,
                blob_requests,
                document_id,
                transaction_msg,
            } => {
                let rasterized_blobs = blob_rasterizer.as_mut().map_or(
                    Vec::new(),
                    |rasterizer| rasterizer.rasterize(&blob_requests),
                );
                eprintln!("Done rasterization ({})", self.name);

                self.tx.send(RasterizationResult::Transaction {
                    blob_rasterizer,
                    rasterized_blobs,
                    document_id,
                    transaction_msg,
                }).unwrap();
                let _ = self.api_tx.send(ApiMsg::WakeUp);

                true
            }
            RasterizationRequest::Stop => {
                false
            }
        }
    }
}
