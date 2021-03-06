/*
 * Copyright (c) 2017 Lima Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "util/ralloc.h"

#include "ppir.h"

ppir_instr *ppir_instr_create(ppir_block *block)
{
   ppir_instr *instr = rzalloc(block, ppir_instr);
   if (!instr)
      return NULL;

   list_inithead(&instr->succ_list);
   list_inithead(&instr->pred_list);

   instr->index = block->comp->cur_instr_index++;
   instr->reg_pressure = -1;

   list_addtail(&instr->list, &block->instr_list);
   return instr;
}

void ppir_instr_add_dep(ppir_instr *succ, ppir_instr *pred)
{
   /* don't add duplicated instr */
   ppir_instr_foreach_pred(succ, dep) {
      if (pred == dep->pred)
         return;
   }

   ppir_dep *dep = ralloc(succ, ppir_dep);
   dep->pred = pred;
   dep->succ = succ;
   list_addtail(&dep->pred_link, &succ->pred_list);
   list_addtail(&dep->succ_link, &pred->succ_list);
}

void ppir_instr_insert_mul_node(ppir_node *add, ppir_node *mul)
{
   ppir_instr *instr = add->instr;
   int pos = mul->instr_pos;
   int *slots = ppir_op_infos[mul->op].slots;

   for (int i = 0; slots[i] != PPIR_INSTR_SLOT_END; i++) {
      /* possible to insert at required place */
      if (slots[i] == pos) {
         if (!instr->slots[pos]) {
            ppir_alu_node *add_alu = ppir_node_to_alu(add);
            ppir_alu_node *mul_alu = ppir_node_to_alu(mul);
            ppir_dest *dest = &mul_alu->dest;
            int pipeline = pos == PPIR_INSTR_SLOT_ALU_VEC_MUL ?
               ppir_pipeline_reg_vmul : ppir_pipeline_reg_fmul;

            /* ^vmul/^fmul can't be used as last arg */
            if (add_alu->num_src > 1) {
               ppir_src *last_src = add_alu->src + add_alu->num_src - 1;
               if (ppir_node_target_equal(last_src, dest))
                  return;
            }

            /* update add node src to use pipeline reg */
            ppir_src *src = add_alu->src;
            if (add_alu->num_src == 3) {
               if (ppir_node_target_equal(src, dest)) {
                  src->type = ppir_target_pipeline;
                  src->pipeline = pipeline;
               }

               if (ppir_node_target_equal(++src, dest)) {
                  src->type = ppir_target_pipeline;
                  src->pipeline = pipeline;
               }
            }
            else {
               assert(ppir_node_target_equal(src, dest));
               src->type = ppir_target_pipeline;
               src->pipeline = pipeline;
            }

            /* update mul node dest to output to pipeline reg */
            dest->type = ppir_target_pipeline;
            dest->pipeline = pipeline;

            instr->slots[pos] = mul;
            mul->instr = instr;
         }
         return;
      }
   }
}

/* check whether a const slot fix into another const slot */
static bool ppir_instr_insert_const(ppir_const *dst, const ppir_const *src,
                                    uint8_t *swizzle)
{
   int i, j;

   for (i = 0; i < src->num; i++) {
      for (j = 0; j < dst->num; j++) {
         if (src->value[i].ui == dst->value[j].ui)
            break;
      }

      if (j == dst->num) {
         if (dst->num == 4)
            return false;
         dst->value[dst->num++] = src->value[i];
      }

      swizzle[i] = j;
   }

   return true;
}

/* make alu node src reflact the pipeline reg */
static void ppir_instr_update_src_pipeline(ppir_instr *instr, ppir_pipeline pipeline,
                                           ppir_dest *dest, uint8_t *swizzle)
{
   for (int i = PPIR_INSTR_SLOT_ALU_START; i <= PPIR_INSTR_SLOT_ALU_END; i++) {
      if (!instr->slots[i])
         continue;

      ppir_alu_node *alu = ppir_node_to_alu(instr->slots[i]);
      for (int j = 0; j < alu->num_src; j++) {
         ppir_src *src = alu->src + j;
         if (ppir_node_target_equal(src, dest)) {
            src->type = ppir_target_pipeline;
            src->pipeline = pipeline;

            if (swizzle) {
               for (int k = 0; k < 4; k++)
                  src->swizzle[k] = swizzle[src->swizzle[k]];
            }
         }
      }
   }
}

bool ppir_instr_insert_node(ppir_instr *instr, ppir_node *node)
{
   if (node->op == ppir_op_const) {
      int i;
      ppir_const_node *c = ppir_node_to_const(node);
      const ppir_const *nc = &c->constant;

      for (i = 0; i < 2; i++) {
         ppir_const ic = instr->constant[i];
         uint8_t swizzle[4] = {0};

         if (ppir_instr_insert_const(&ic, nc, swizzle)) {
            instr->constant[i] = ic;
            ppir_instr_update_src_pipeline(
               instr, ppir_pipeline_reg_const0 + i, &c->dest, swizzle);
            break;
         }
      }

      /* no const slot can insert */
      if (i == 2)
         return false;

      return true;
   }
   else {
      int *slots = ppir_op_infos[node->op].slots;
      for (int i = 0; slots[i] != PPIR_INSTR_SLOT_END; i++) {
         int pos = slots[i];

         if (instr->slots[pos]) {
            /* node already in this instr, i.e. load_uniform */
            if (instr->slots[pos] == node)
               return true;
            else
               continue;
         }

         if (pos == PPIR_INSTR_SLOT_ALU_SCL_MUL ||
             pos == PPIR_INSTR_SLOT_ALU_SCL_ADD) {
            ppir_dest *dest = ppir_node_get_dest(node);
            if (!ppir_target_is_scaler(dest))
               continue;
         }

         instr->slots[pos] = node;
         node->instr = instr;
         node->instr_pos = pos;

         if (node->op == ppir_op_load_uniform) {
            ppir_load_node *l = ppir_node_to_load(node);
            ppir_instr_update_src_pipeline(
               instr, ppir_pipeline_reg_uniform, &l->dest, NULL);
         }

         return true;
      }

      return false;
   }
}

static struct {
   int len;
   char *name;
} ppir_instr_fields[] = {
   [PPIR_INSTR_SLOT_VARYING] = { 4, "vary" },
   [PPIR_INSTR_SLOT_TEXLD] = { 4, "texl"},
   [PPIR_INSTR_SLOT_UNIFORM] = { 4, "unif" },
   [PPIR_INSTR_SLOT_ALU_VEC_MUL] = { 4, "vmul" },
   [PPIR_INSTR_SLOT_ALU_SCL_MUL] = { 4, "smul" },
   [PPIR_INSTR_SLOT_ALU_VEC_ADD] = { 4, "vadd" },
   [PPIR_INSTR_SLOT_ALU_SCL_ADD] = { 4, "sadd" },
   [PPIR_INSTR_SLOT_ALU_COMBINE] = { 4, "comb" },
   [PPIR_INSTR_SLOT_STORE_TEMP] = { 4, "stor" },
};

void ppir_instr_print_list(ppir_compiler *comp)
{
   if (!lima_shader_debug_pp)
      return;

   printf("======ppir instr list======\n");
   printf("      ");
   for (int i = 0; i < PPIR_INSTR_SLOT_NUM; i++)
      printf("%-*s ", ppir_instr_fields[i].len, ppir_instr_fields[i].name);
   printf("const0|1\n");

   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_instr, instr, &block->instr_list, list) {
         printf("%c%03d: ", instr->is_end ? '*' : ' ', instr->index);
         for (int i = 0; i < PPIR_INSTR_SLOT_NUM; i++) {
            ppir_node *node = instr->slots[i];
            if (node)
               printf("%-*d ", ppir_instr_fields[i].len, node->index);
            else
               printf("%-*s ", ppir_instr_fields[i].len, "null");
         }
         for (int i = 0; i < 2; i++) {
            if (i)
               printf("| ");

            for (int j = 0; j < instr->constant[i].num; j++)
               printf("%f ", instr->constant[i].value[j].f);
         }
         printf("\n");
      }
      printf("------------------------\n");
   }
}

static void ppir_instr_print_sub(ppir_instr *instr)
{
   printf("[%s%d",
          instr->printed && !ppir_instr_is_leaf(instr) ? "+" : "",
          instr->index);

   if (!instr->printed) {
      ppir_instr_foreach_pred(instr, dep) {
         ppir_instr_print_sub(dep->pred);
      }

      instr->printed = true;
   }

   printf("]");
}

void ppir_instr_print_dep(ppir_compiler *comp)
{
   if (!lima_shader_debug_pp)
      return;

   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_instr, instr, &block->instr_list, list) {
         instr->printed = false;
      }
   }

   printf("======ppir instr depend======\n");
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_instr, instr, &block->instr_list, list) {
         if (ppir_instr_is_root(instr)) {
            ppir_instr_print_sub(instr);
            printf("\n");
         }
      }
      printf("------------------------\n");
   }
}
