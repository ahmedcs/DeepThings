#include "reuse_data_serialization.h"
#if DATA_REUSE


blob* self_reuse_data_serialization(cnn_model* model, uint32_t task_id, uint32_t frame_num){
   ftp_parameters_reuse* ftp_para_reuse = model->ftp_para_reuse;
   network_parameters* net_para = model->net_para;
   overlapped_tile_data regions_and_data;
   tile_region overlap_index;
   uint32_t i = task_id / (ftp_para_reuse->partitions_w); 
   uint32_t j = task_id % (ftp_para_reuse->partitions_w);
   int32_t adjacent_id[4];
   uint32_t position;
   float *reuse_data;
   uint32_t size = 0;
   uint32_t l;
   reuse_data = (float*)malloc(ftp_para_reuse->self_reuse_data_size[task_id]);

   for(position = 0; position < 4; position++) 
      adjacent_id[position]=-1;

   /*get the up overlapped data from tile below*/
   if((i+1)<(ftp_para_reuse->partitions_h)) adjacent_id[0] = ftp_para_reuse->task_id[i+1][j];
   /*get the left overlapped data from tile on the right*/
   if((j+1)<(ftp_para_reuse->partitions_w)) adjacent_id[1] = ftp_para_reuse->task_id[i][j+1];
   /*get the bottom overlapped data from tile above*/
   if(i>0) adjacent_id[2] = ftp_para_reuse->task_id[i-1][j];
   /*get the right overlapped data from tile on the left*/
   if(j>0) adjacent_id[3] = ftp_para_reuse->task_id[i][j-1];

   for(l = 0; l < ftp_para_reuse->fused_layers-1; l ++){
      for(position = 0; position < 4; position++){
         if(adjacent_id[position]==-1) continue;
         regions_and_data = ftp_para_reuse->output_reuse_regions[task_id][l];
         overlap_index = get_region(&regions_and_data, position);
         if((overlap_index.w>0)&&(overlap_index.h>0)){
            uint32_t amount_of_element = overlap_index.w*overlap_index.h*net_para->output_maps[l].c;
#if DEBUG_SERIALIZATION
            if(position==0) printf("Self below overlapped amount is %d \n",amount_of_element);
            if(position==1) printf("Self right overlapped amount is %d \n",amount_of_element);
            if(position==2) printf("Self above overlapped amount is %d \n",amount_of_element);
            if(position==3) printf("Self left overlapped amount is %d \n",amount_of_element);
#endif
            memcpy(reuse_data, get_data(&regions_and_data, position), amount_of_element*sizeof(float) ); 
            reuse_data = reuse_data + amount_of_element;
            size += amount_of_element;

         }
      }
   }
   reuse_data = reuse_data - size;
   size = (size) * sizeof(float);
   blob* temp = new_blob_and_copy_data((int32_t)task_id, size, (uint8_t*)reuse_data);
   annotate_blob(temp, get_this_client_id(), frame_num, task_id);
   free(reuse_data);
   return temp;
}

overlapped_tile_data* self_reuse_data_deserialization(cnn_model* model, uint32_t task_id, float* input, uint32_t frame_num){
   ftp_parameters_reuse* ftp_para_reuse = model->ftp_para_reuse;
   network_parameters* net_para = model->net_para;

   tile_region overlap_index;
   uint32_t i = task_id / (ftp_para_reuse->partitions_w); 
   uint32_t j = task_id % (ftp_para_reuse->partitions_w);
   int32_t adjacent_id[4];
   uint32_t position;
   uint32_t l;
   float* serial_data = input;

   overlapped_tile_data* regions_and_data_ptr = (overlapped_tile_data*)malloc(sizeof(overlapped_tile_data)*(ftp_para_reuse->fused_layers));
   for(position = 0; position < 4; position++){
      adjacent_id[position]=-1;
   }

   /*get the up overlapped data from tile below*/
   if((i+1)<(ftp_para_reuse->partitions_h)) adjacent_id[0] = ftp_para_reuse->task_id[i+1][j];
   /*get the left overlapped data from tile on the right*/
   if((j+1)<(ftp_para_reuse->partitions_w)) adjacent_id[1] = ftp_para_reuse->task_id[i][j+1];
   /*get the bottom overlapped data from tile above*/
   if(i>0) adjacent_id[2] = ftp_para_reuse->task_id[i-1][j];
   /*get the right overlapped data from tile on the left*/
   if(j>0) adjacent_id[3] = ftp_para_reuse->task_id[i][j-1];

   for(l = 0; l < ftp_para_reuse->fused_layers-1; l ++){
      for(position = 0; position < 4; position++){
         if(adjacent_id[position]==-1) continue;
         overlapped_tile_data original = ftp_para_reuse->output_reuse_regions[task_id][l];
         overlap_index = get_region(&original, position);
         if((overlap_index.w>0)&&(overlap_index.h>0)){
            uint32_t amount_of_element = overlap_index.w*overlap_index.h*net_para->output_maps[l].c;
#if DEBUG_SERIALIZATION
            if(position==0) printf("Self below overlapped amount is %d \n",amount_of_element);
            if(position==1) printf("Self right overlapped amount is %d \n",amount_of_element);
            if(position==2) printf("Self above overlapped amount is %d \n",amount_of_element);
            if(position==3) printf("Self left overlapped amount is %d \n",amount_of_element);
#endif
            float* data = (float* )malloc(amount_of_element*sizeof(float));
            memcpy(data, serial_data, amount_of_element*sizeof(float)); 
            serial_data = serial_data + amount_of_element;
            set_size(regions_and_data_ptr+l, position, amount_of_element*sizeof(float));
            set_data(regions_and_data_ptr+l, position, data);
         }
      }
   }

   return regions_and_data_ptr;
}

void place_self_deserialized_data(cnn_model* model, uint32_t task_id, overlapped_tile_data* regions_and_data_ptr){
   ftp_parameters_reuse* ftp_para_reuse = model->ftp_para_reuse;
   overlapped_tile_data regions_and_data;
   overlapped_tile_data regions_and_data_to_be_placed;
   tile_region overlap_index;

   uint32_t i = task_id / (ftp_para_reuse->partitions_w); 
   uint32_t j = task_id % (ftp_para_reuse->partitions_w);
   int32_t adjacent_id[4];
   uint32_t position;
   uint32_t l;
   for(position = 0; position < 4; position++) 
      adjacent_id[position]=-1;
   /*get the up overlapped data from tile below*/
   if((i+1)<(ftp_para_reuse->partitions_h)) adjacent_id[0] = ftp_para_reuse->task_id[i+1][j];
   /*get the left overlapped data from tile on the right*/
   if((j+1)<(ftp_para_reuse->partitions_w)) adjacent_id[1] = ftp_para_reuse->task_id[i][j+1];
   /*get the bottom overlapped data from tile above*/
   if(i>0) adjacent_id[2] = ftp_para_reuse->task_id[i-1][j];
   /*get the right overlapped data from tile on the left*/
   if(j>0) adjacent_id[3] = ftp_para_reuse->task_id[i][j-1];

   for(l = 0; l < ftp_para_reuse->fused_layers-1; l ++){
      for(position = 0; position < 4; position++){
         if(adjacent_id[position]==-1) continue;
         regions_and_data = ftp_para_reuse->output_reuse_regions[task_id][l];
         overlap_index = get_region(&regions_and_data, position);
         if((overlap_index.w>0)&&(overlap_index.h>0)){
            regions_and_data_to_be_placed = regions_and_data_ptr[l];
            uint32_t size = get_size(&regions_and_data_to_be_placed, position);
            float* data = get_data(&regions_and_data_to_be_placed, position);
#if DEBUG_SERIALIZATION
            uint32_t amount_of_element = size/sizeof(float);
            if(position==0) printf("Place self below overlapped amount is %d at layer %d\n",amount_of_element, l);
            if(position==1) printf("Place self right overlapped amount is %d at layer %d\n",amount_of_element, l);
            if(position==2) printf("Place self above overlapped amount is %d at layer %d\n",amount_of_element, l);
            if(position==3) printf("Place self left overlapped amount is %d at layer %d\n",amount_of_element, l);
#endif
            if(get_size(&regions_and_data, position)>0) {
#if DEBUG_SERIALIZATION
               printf("free self old data for partition %ld \n", get_size(&regions_and_data, position)/sizeof(float));
#endif
               free(get_data(&regions_and_data, position));
               set_size(&regions_and_data, position, 0);
            }
            set_data(&regions_and_data, position, data);
            set_size(&regions_and_data, position, size);
         }
         ftp_para_reuse->output_reuse_regions[task_id][l] = regions_and_data;
      }
   }

}


#endif

