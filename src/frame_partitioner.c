#include "darkiot.h"
#include "frame_partitioner.h"

void partition_and_enqueue(cnn_model* model, uint32_t frame_num){
   uint32_t task;
   network_parameters* net_para = model->net_para;
   float* data;
   uint32_t data_size;
   blob* temp;
   uint32_t dw1, dw2;
   uint32_t dh1, dh2;
   uint32_t i, j;
   for(i = 0; i < model->ftp_para->partitions_h; i++){
      for(j = 0; j < model->ftp_para->partitions_w; j++){
         task = model->ftp_para->task_id[i][j];
         dw1 = model->ftp_para->input_tiles[task][0].w1;
         dw2 = model->ftp_para->input_tiles[task][0].w2;
         dh1 = model->ftp_para->input_tiles[task][0].h1;
         dh2 = model->ftp_para->input_tiles[task][0].h2;
         data = crop_feature_maps(get_model_input(model), 
                                  net_para->input_maps[0].w, 
                                  net_para->input_maps[0].h,
                                  net_para->input_maps[0].c, 
                                  dw1, dw2, dh1, dh2);
         data_size = sizeof(float)*(dw2-dw1+1)*(dh2-dh1+1)*net_para->input_maps[0].c;
         temp = new_blob_and_copy_data((int32_t)task, data_size, (uint8_t*)data);
         free(data);
         annotate_blob(temp, get_this_client_id(), frame_num, task);
         enqueue(task_queue, temp);
         free_blob(temp);
      }

   }
#if DATA_REUSE
   for(i = 0; i < model->ftp_para_reuse->partitions_h; i++){
      for(j = 0; j < model->ftp_para_reuse->partitions_w; j++){
         task = model->ftp_para_reuse->task_id[i][j];
         if(model->ftp_para_reuse->schedule[task] == 1){
            remove_by_id(task_queue, task);
            /*Enqueue original size for rollback execution if adjacent partition is not ready... ...*/
            dw1 = model->ftp_para->input_tiles[task][0].w1;
            dw2 = model->ftp_para->input_tiles[task][0].w2;
            dh1 = model->ftp_para->input_tiles[task][0].h1;
            dh2 = model->ftp_para->input_tiles[task][0].h2;
            data = crop_feature_maps(get_model_input(model), 
                                  net_para->input_maps[0].w, 
                                  net_para->input_maps[0].h,
                                  net_para->input_maps[0].c, 
                                  dw1, dw2, dh1, dh2);
            data_size = sizeof(float)*(dw2-dw1+1)*(dh2-dh1+1)*net_para->input_maps[0].c;
            temp = new_blob_and_copy_data((int32_t)task, data_size, (uint8_t*)data);
            free(data);
            annotate_blob(temp, get_this_client_id(), frame_num, task);
            enqueue(task_queue, temp);
            free_blob(temp);
        }
      }
   }


   ftp_parameters_reuse* ftp_para_reuse = model->ftp_para_reuse;
   clean_coverage(ftp_para_reuse);
   for(i = 0; i < ftp_para_reuse->partitions_h; i++){
      for(j = 0; j < ftp_para_reuse->partitions_w; j++){
         task = ftp_para_reuse->task_id[i][j];
         if(ftp_para_reuse->schedule[task] == 1){
            dw1 = ftp_para_reuse->input_tiles[task][0].w1;
            dw2 = ftp_para_reuse->input_tiles[task][0].w2;
            dh1 = ftp_para_reuse->input_tiles[task][0].h1;
            dh2 = ftp_para_reuse->input_tiles[task][0].h2;
            ftp_para_reuse->shrinked_input[task] = 
                                  crop_feature_maps(get_model_input(model), 
                                  net_para->input_maps[0].w, 
                                  net_para->input_maps[0].h,
                                  net_para->input_maps[0].c, 
                                  dw1, dw2, dh1, dh2);
            ftp_para_reuse->shrinked_input_size[task] = 
                          sizeof(float)*(dw2-dw1+1)*(dh2-dh1+1)*net_para->input_maps[0].c;
         }
      }
   }
#endif

}

float* dequeue_and_merge(cnn_model* model){
   /*Check if there is a data frame whose tasks have all been collected*/
   blob* temp = dequeue(ready_pool);
   int32_t cli_id = temp->id;
   free_blob(temp);
#if DEBUG_FLAG
   printf("Results for client %d are all collected\n", cli_id);
#endif
   ftp_parameters *ftp_para = model->ftp_para;
   network_parameters *net_para = model->net_para;


   uint32_t stage_outs =  (net_para->output_maps[ftp_para->fused_layers-1].w)*(net_para->output_maps[ftp_para->fused_layers-1].h)*(net_para->output_maps[ftp_para->fused_layers-1].c);
   float* stage_out = (float*) malloc(sizeof(float)*stage_outs);  
   uint32_t part = 0;
   uint32_t task = 0;
   float* cropped_output;

   for(part = 0; part < ftp_para->partitions; part ++){
      temp = dequeue(results_pool[cli_id]);
      task = get_blob_task_id(temp);
      

      if(net_para->type[ftp_para->fused_layers-1] == CONV_LAYER){
         tile_region tmp = relative_offsets(ftp_para->input_tiles[task][ftp_para->fused_layers-1], 
                                       ftp_para->output_tiles[task][ftp_para->fused_layers-1]);  
         cropped_output = crop_feature_maps((float*)temp->data, 
                      ftp_para->input_tiles[task][ftp_para->fused_layers-1].w, 
                      ftp_para->input_tiles[task][ftp_para->fused_layers-1].h, 
                      net_para->output_maps[ftp_para->fused_layers-1].c, 
                      tmp.w1, tmp.w2, tmp.h1, tmp.h2);
      }else{cropped_output = (float*)temp->data;}

      stitch_feature_maps(cropped_output, stage_out, 
                          net_para->output_maps[ftp_para->fused_layers-1].w, 
                          net_para->output_maps[ftp_para->fused_layers-1].h, 
                          net_para->output_maps[ftp_para->fused_layers-1].c, 
                          ftp_para->output_tiles[task][ftp_para->fused_layers-1].w1, 
                          ftp_para->output_tiles[task][ftp_para->fused_layers-1].w2,
                          ftp_para->output_tiles[task][ftp_para->fused_layers-1].h1, 
                          ftp_para->output_tiles[task][ftp_para->fused_layers-1].h2);

      if(net_para->type[ftp_para->fused_layers-1] == CONV_LAYER){free(cropped_output);}

      free_blob(temp);
   }
   return stage_out;
}

