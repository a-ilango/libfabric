/*
 * Copyright (c) 2015-2016 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>

#include <fi_util.h>

static int fi_valid_addr_format(uint32_t prov_format, uint32_t user_format)
{
	if (user_format == FI_FORMAT_UNSPEC)
		return 1;

	switch (prov_format) {
	case FI_SOCKADDR:
		/* Provider supports INET and INET6 */
		return user_format <= FI_SOCKADDR_IN6;
	case FI_SOCKADDR_IN:
		/* Provider supports INET only */
		return user_format <= FI_SOCKADDR_IN;
	case FI_SOCKADDR_IN6:
		/* Provider supports INET6 only */
		return user_format <= FI_SOCKADDR_IN6;
	case FI_SOCKADDR_IB:
		/* Provider must support IB, INET, and INET6 */
		return user_format <= FI_SOCKADDR_IB;
	default:
		return prov_format == user_format;
	}
}

/*
 * Accepts a string which has tokens delimited by underscores and returns a token
 * array, tok[0..max_token - 1]. Callers are responsible for freeing tok[0] which
 * will point to a fudged copy of the string. Refer strtok_r manpage for details.
 */
int utilx_parse_name(const char *name, int max_tok, char **tok, int get_prefix)
{
	char *name_dup, *name_ref, *save_ptr;
	char *delim = "_";
	int i, parsed_len = 0;

	name_dup = strdup(name);
	if (!name_dup)
		return -FI_ENOMEM;

	name_ref = name_dup;
	for (i = 0; i < max_tok; name_ref = NULL, i++) {
		tok[i] = strtok_r(name_ref, delim, &save_ptr);
		if (!tok[i])
			break;
	}
	if (i < max_tok)
		goto err;

	if (!get_prefix) {
		/* Handle the case where there is a delim in token itself */
		for (i = 0; i < max_tok; i++)
			parsed_len += strlen(tok[i]) + 1;
		parsed_len--;

		if (strlen(name) > parsed_len)
			tok[max_tok - 1][strlen(tok[max_tok - 1])] = *delim;
	}

	return 0;
err:
	free(name_dup);
	return -FI_EOTHER;
}

int utilx_tr_layer_domain_attr(struct fi_domain_attr *layer_attr,
		struct fi_domain_attr *base_attr)
{
	char *tok[2];
	int ret;

	ret = utilx_parse_name(layer_attr->name, 2, tok, 0);
	if (ret)
		return ret;
	if (!(base_attr->name = strdup(tok[1])))
		ret = -FI_ENOMEM;
	free(tok[0]);
	return ret;
}

int utilx_tr_layer_fabric_attr(struct fi_fabric_attr *layer_attr,
		struct fi_fabric_attr *base_attr)
{
	char *tok[3];
	int ret;

	ret = utilx_parse_name(layer_attr->name, 3, tok, 0);
	if (ret)
		return ret;
	if (!(base_attr->name = strdup(tok[2]))) {
		ret = -FI_ENOMEM;
		goto out;
	}
	if (!(base_attr->prov_name = strdup(tok[1])))
		ret = -FI_ENOMEM;
out:
	free(tok[0]);
	return ret;
}

int utilx_tr_base_domain_attr(struct fi_domain_attr *base_attr, char *prefix,
		struct fi_domain_attr *layer_attr)
{
	size_t len;
	len = strlen(prefix) + strlen(base_attr->name) + 2;
	if (!(layer_attr->name = malloc(len)))
		return -FI_ENOMEM;
	snprintf(layer_attr->name, len, "%s_%s", prefix, base_attr->name);
	return 0;
}

/* Construct fabric name for the layered provider in the following format:
 * <layered_prov>_<base_prov_name>_<base_prov_fabric_name>
 * e.g. rxm_verbs_IB-1234 */
int utilx_tr_base_fabric_attr(struct fi_fabric_attr *base_attr, char *prefix,
		struct fi_fabric_attr *layer_attr)
{
	size_t len;
	len = strlen(prefix) + strlen(base_attr->name) +
		strlen(base_attr->prov_name) + 3;
	if (!(layer_attr->name = malloc(len)))
		return -FI_ENOMEM;
	snprintf(layer_attr->name, len, "%s_%s_%s", prefix, base_attr->prov_name,
			base_attr->name);
	return 0;
}

int utilx_getinfo(uint32_t version, const char *node, const char *service,
			uint64_t flags, const struct fi_provider *prov,
			const struct fi_info *prov_info, struct fi_info *hints,
			utilx_tr_layer_info_t tr_layer_info,
			utilx_tr_base_info_t tr_base_info,
			int get_base_info, struct fi_info **info)
{
	struct fi_info *base_hints, *base_info;
	int ret;

	ret = fi_check_info(prov, prov_info, hints, FI_CHECK_LAYERED);
	if (ret)
		return ret;

	if (!(base_hints = tr_layer_info(hints)))
		return -FI_ENOMEM;

	ret = fi_getinfo(version, node, service, flags, base_hints, &base_info);
	if (ret)
		goto out;

	if (get_base_info) {
		*info = base_info;
	} else {
		if (!(*info = tr_base_info(base_info))) {
			ret = -FI_ENOMEM;
			goto out;
		}
		fi_freeinfo(base_info);
	}
out:
	fi_freeinfo(base_hints);
	return ret;
}

int fi_check_name(char *user_name, char *prov_name, enum fi_check_type type)
{
	char *tok[1];
	int ret;

	if (type == FI_CHECK_LAYERED) {
		ret = utilx_parse_name(user_name, 1, tok, 1);
		if (ret)
			return ret;

		ret = strcasecmp(prov_name, tok[0]);
		free(tok[0]);
		return ret;
	} else {
		return strcasecmp(prov_name, user_name);
	}
}

int fi_check_fabric_attr(const struct fi_provider *prov,
			 const struct fi_fabric_attr *prov_attr,
			 const struct fi_fabric_attr *user_attr,
			 enum fi_check_type type)
{
	if (user_attr->name && fi_check_name(user_attr->name, prov_attr->name, type)) {
		FI_INFO(prov, FI_LOG_CORE, "Unknown fabric name\n");
		return -FI_ENODATA;
	}

	if (user_attr->prov_version > prov_attr->prov_version) {
		FI_INFO(prov, FI_LOG_CORE, "Unsupported provider version\n");
		return -FI_ENODATA;
	}

	return 0;
}

/*
 * Threading models ranked by order of parallelism.
 */
static int fi_thread_level(enum fi_threading thread_model)
{
	switch (thread_model) {
	case FI_THREAD_SAFE:
		return 1;
	case FI_THREAD_FID:
		return 2;
	case FI_THREAD_ENDPOINT:
		return 3;
	case FI_THREAD_COMPLETION:
		return 4;
	case FI_THREAD_DOMAIN:
		return 5;
	case FI_THREAD_UNSPEC:
		return 6;
	default:
		return -1;
	}
}

/*
 * Progress models ranked by order of automation.
 */
static int fi_progress_level(enum fi_progress progress_model)
{
	switch (progress_model) {
	case FI_PROGRESS_AUTO:
		return 1;
	case FI_PROGRESS_MANUAL:
		return 2;
	case FI_PROGRESS_UNSPEC:
		return 3;
	default:
		return -1;
	}
}

/*
 * Resource management models ranked by order of enablement.
 */
static int fi_resource_mgmt_level(enum fi_resource_mgmt rm_model)
{
	switch (rm_model) {
	case FI_RM_ENABLED:
		return 1;
	case FI_RM_DISABLED:
		return 2;
	case FI_RM_UNSPEC:
		return 3;
	default:
		return -1;
	}
}

int fi_check_domain_attr(const struct fi_provider *prov,
			 const struct fi_domain_attr *prov_attr,
			 const struct fi_domain_attr *user_attr,
			 enum fi_check_type type)
{
	if (user_attr->name && fi_check_name(user_attr->name, prov_attr->name, type)) {
		FI_INFO(prov, FI_LOG_CORE, "Unknown domain name\n");
		return -FI_ENODATA;
	}

	if (fi_thread_level(user_attr->threading) <
	    fi_thread_level(prov_attr->threading)) {
		FI_INFO(prov, FI_LOG_CORE, "Invalid threading model\n");
		return -FI_ENODATA;
	}

	if (fi_progress_level(user_attr->control_progress) <
	    fi_progress_level(prov_attr->control_progress)) {
		FI_INFO(prov, FI_LOG_CORE, "Invalid control progress model\n");
		return -FI_ENODATA;
	}

	if (fi_progress_level(user_attr->data_progress) <
	    fi_progress_level(prov_attr->data_progress)) {
		FI_INFO(prov, FI_LOG_CORE, "Invalid data progress model\n");
		return -FI_ENODATA;
	}

	if (fi_resource_mgmt_level(user_attr->resource_mgmt) <
	    fi_resource_mgmt_level(prov_attr->resource_mgmt)) {
		FI_INFO(prov, FI_LOG_CORE, "Invalid resource mgmt model\n");
		return -FI_ENODATA;
	}

	if ((prov_attr->av_type != FI_AV_UNSPEC) &&
	    (user_attr->av_type != FI_AV_UNSPEC) &&
	    (prov_attr->av_type != user_attr->av_type)) {
		FI_INFO(prov, FI_LOG_CORE, "Invalid AV type\n");
	   	return -FI_ENODATA;
	}

	if (user_attr->mr_mode && (user_attr->mr_mode != prov_attr->mr_mode)) {
		FI_INFO(prov, FI_LOG_CORE, "Invalid memory registration mode\n");
		return -FI_ENODATA;
	}

	if (user_attr->cq_data_size > prov_attr->cq_data_size) {
		FI_INFO(prov, FI_LOG_CORE, "CQ data size too large\n");
		return -FI_ENODATA;
	}

	return 0;
}

int fi_check_ep_attr(const struct fi_provider *prov,
		     const struct fi_ep_attr *prov_attr,
		     const struct fi_ep_attr *user_attr)
{
	if (user_attr->type && (user_attr->type != prov_attr->type)) {
		FI_INFO(prov, FI_LOG_CORE, "Unsupported endpoint type\n");
		return -FI_ENODATA;
	}

	if (user_attr->protocol && (user_attr->protocol != prov_attr->protocol)) {
		FI_INFO(prov, FI_LOG_CORE, "Unsupported protocol\n");
		return -FI_ENODATA;
	}

	if (user_attr->protocol_version &&
	    (user_attr->protocol_version > prov_attr->protocol_version)) {
		FI_INFO(prov, FI_LOG_CORE, "Unsupported protocol version\n");
		return -FI_ENODATA;
	}

	if (user_attr->max_msg_size > prov_attr->max_msg_size) {
		FI_INFO(prov, FI_LOG_CORE, "Max message size too large\n");
		return -FI_ENODATA;
	}

	return 0;
}

int fi_check_rx_attr(const struct fi_provider *prov,
		     const struct fi_rx_attr *prov_attr,
		     const struct fi_rx_attr *user_attr)
{
	if (user_attr->caps & ~(prov_attr->caps)) {
		FI_INFO(prov, FI_LOG_CORE, "caps not supported\n");
		FI_INFO(prov, FI_LOG_CORE, "Supported: %s\n", fi_tostr(&prov_attr->caps, FI_TYPE_CAPS));
		FI_INFO(prov, FI_LOG_CORE, "Requested: %s\n", fi_tostr(&user_attr->caps, FI_TYPE_CAPS));
		return -FI_ENODATA;
	}

	if ((user_attr->mode & prov_attr->mode) != prov_attr->mode) {
		FI_INFO(prov, FI_LOG_CORE, "needed mode not set\n");
		FI_INFO(prov, FI_LOG_CORE, "Expected: %s\n", fi_tostr(&prov_attr->mode, FI_TYPE_MODE));
		FI_INFO(prov, FI_LOG_CORE, "Given: %s\n", fi_tostr(&user_attr->mode, FI_TYPE_MODE));
		return -FI_ENODATA;
	}

	if (prov_attr->op_flags & ~(prov_attr->op_flags)) {
		FI_INFO(prov, FI_LOG_CORE, "op_flags not supported\n");
		FI_INFO(prov, FI_LOG_CORE, "Supported: %s\n", fi_tostr(&prov_attr->op_flags, FI_TYPE_OP_FLAGS));
		FI_INFO(prov, FI_LOG_CORE, "Requested: %s\n", fi_tostr(&user_attr->op_flags, FI_TYPE_OP_FLAGS));
		return -FI_ENODATA;
	}

	if (user_attr->msg_order & ~(prov_attr->msg_order)) {
		FI_INFO(prov, FI_LOG_CORE, "msg_order not supported\n");
		FI_INFO(prov, FI_LOG_CORE, "Supported: %s\n", fi_tostr(&prov_attr->msg_order, FI_TYPE_MSG_ORDER));
		FI_INFO(prov, FI_LOG_CORE, "Requested: %s\n", fi_tostr(&user_attr->msg_order, FI_TYPE_MSG_ORDER));
		return -FI_ENODATA;
	}

	if (user_attr->comp_order & ~(prov_attr->comp_order)) {
		FI_INFO(prov, FI_LOG_CORE, "comp_order not supported\n");
		FI_INFO(prov, FI_LOG_CORE, "Supported: %s\n", fi_tostr(&prov_attr->comp_order, FI_TYPE_MSG_ORDER));
		FI_INFO(prov, FI_LOG_CORE, "Requested: %s\n", fi_tostr(&user_attr->comp_order, FI_TYPE_MSG_ORDER));
		return -FI_ENODATA;
	}

	if (user_attr->total_buffered_recv > prov_attr->total_buffered_recv) {
		FI_INFO(prov, FI_LOG_CORE, "total_buffered_recv too large\n");
		return -FI_ENODATA;
	}

	if (user_attr->size > prov_attr->size) {
		FI_INFO(prov, FI_LOG_CORE, "size is greater than supported\n");
		return -FI_ENODATA;
	}

	if (user_attr->iov_limit > prov_attr->iov_limit) {
		FI_INFO(prov, FI_LOG_CORE, "iov_limit too large\n");
		return -FI_ENODATA;
	}

	return 0;
}

int fi_check_tx_attr(const struct fi_provider *prov,
		     const struct fi_tx_attr *prov_attr,
		     const struct fi_tx_attr *user_attr)
{
	if (user_attr->caps & ~(prov_attr->caps)) {
		FI_INFO(prov, FI_LOG_CORE, "caps not supported\n");
		FI_INFO(prov, FI_LOG_CORE, "Supported: %s\n", fi_tostr(&prov_attr->caps, FI_TYPE_CAPS));
		FI_INFO(prov, FI_LOG_CORE, "Requested: %s\n", fi_tostr(&user_attr->caps, FI_TYPE_CAPS));
		return -FI_ENODATA;
	}

	if ((user_attr->mode & prov_attr->mode) != prov_attr->mode) {
		FI_INFO(prov, FI_LOG_CORE, "needed mode not set\n");
		FI_INFO(prov, FI_LOG_CORE, "Expected: %s\n", fi_tostr(&prov_attr->mode, FI_TYPE_MODE));
		FI_INFO(prov, FI_LOG_CORE, "Given: %s\n", fi_tostr(&user_attr->mode, FI_TYPE_MODE));
		return -FI_ENODATA;
	}

	if (prov_attr->op_flags & ~(prov_attr->op_flags)) {
		FI_INFO(prov, FI_LOG_CORE, "op_flags not supported\n");
		FI_INFO(prov, FI_LOG_CORE, "Supported: %s\n", fi_tostr(&prov_attr->op_flags, FI_TYPE_OP_FLAGS));
		FI_INFO(prov, FI_LOG_CORE, "Requested: %s\n", fi_tostr(&user_attr->op_flags, FI_TYPE_OP_FLAGS));
		return -FI_ENODATA;
	}

	if (user_attr->msg_order & ~(prov_attr->msg_order)) {
		FI_INFO(prov, FI_LOG_CORE, "msg_order not supported\n");
		FI_INFO(prov, FI_LOG_CORE, "Supported: %s\n", fi_tostr(&prov_attr->msg_order, FI_TYPE_MSG_ORDER));
		FI_INFO(prov, FI_LOG_CORE, "Requested: %s\n", fi_tostr(&user_attr->msg_order, FI_TYPE_MSG_ORDER));
		return -FI_ENODATA;
	}

	if (user_attr->comp_order & ~(prov_attr->comp_order)) {
		FI_INFO(prov, FI_LOG_CORE, "comp_order not supported\n");
		FI_INFO(prov, FI_LOG_CORE, "Supported: %s\n", fi_tostr(&prov_attr->comp_order, FI_TYPE_MSG_ORDER));
		FI_INFO(prov, FI_LOG_CORE, "Requested: %s\n", fi_tostr(&user_attr->comp_order, FI_TYPE_MSG_ORDER));
		return -FI_ENODATA;
	}

	if (user_attr->inject_size > prov_attr->inject_size) {
		FI_INFO(prov, FI_LOG_CORE, "inject_size too large\n");
		return -FI_ENODATA;
	}

	if (user_attr->size > prov_attr->size) {
		FI_INFO(prov, FI_LOG_CORE, "size is greater than supported\n");
		return -FI_ENODATA;
	}

	if (user_attr->iov_limit > prov_attr->iov_limit) {
		FI_INFO(prov, FI_LOG_CORE, "iov_limit too large\n");
		return -FI_ENODATA;
	}

	if (user_attr->rma_iov_limit > prov_attr->rma_iov_limit) {
		FI_INFO(prov, FI_LOG_CORE, "rma_iov_limit too large\n");
		return -FI_ENODATA;
	}

	return 0;
}

int fi_check_info(const struct fi_provider *prov,
		  const struct fi_info *prov_info,
		  const struct fi_info *user_info,
		  enum fi_check_type type)
{
	int ret;

	if (!user_info)
		return 0;

	if (user_info->caps & ~(prov_info->caps)) {
		FI_INFO(prov, FI_LOG_CORE, "Unsupported capabilities\n");
		FI_INFO(prov, FI_LOG_CORE, "Supported: %s\n", fi_tostr(&prov_info->caps, FI_TYPE_CAPS));
		FI_INFO(prov, FI_LOG_CORE, "Requested: %s\n", fi_tostr(&user_info->caps, FI_TYPE_CAPS));
		return -FI_ENODATA;
	}

	if ((user_info->mode & prov_info->mode) != prov_info->mode) {
		FI_INFO(prov, FI_LOG_CORE, "needed mode not set\n");
		FI_INFO(prov, FI_LOG_CORE, "Expected: %s\n", fi_tostr(&prov_info->mode, FI_TYPE_MODE));
		FI_INFO(prov, FI_LOG_CORE, "Given: %s\n", fi_tostr(&user_info->mode, FI_TYPE_MODE));
		return -FI_ENODATA;
	}

	if (!fi_valid_addr_format(prov_info->addr_format,
				  user_info->addr_format)) {
		FI_INFO(prov, FI_LOG_CORE, "address format not supported\n");
		return -FI_ENODATA;
	}

	if (user_info->fabric_attr) {
		ret = fi_check_fabric_attr(prov, prov_info->fabric_attr,
					   user_info->fabric_attr,
					   type);
		if (ret)
			return ret;
	}

	if (user_info->domain_attr) {
		ret = fi_check_domain_attr(prov, prov_info->domain_attr,
					   user_info->domain_attr,
					   type);
		if (ret)
			return ret;
	}

	if (user_info->ep_attr) {
		ret = fi_check_ep_attr(prov, prov_info->ep_attr,
				       user_info->ep_attr);
		if (ret)
			return ret;
	}

	if (user_info->rx_attr) {
		ret = fi_check_rx_attr(prov, prov_info->rx_attr,
				       user_info->rx_attr);
		if (ret)
			return ret;
	}

	if (user_info->tx_attr) {
		ret = fi_check_tx_attr(prov, prov_info->tx_attr,
				       user_info->tx_attr);
		if (ret)
			return ret;
	}

	return 0;
}

static void fi_alter_ep_attr(struct fi_ep_attr *attr,
			     const struct fi_ep_attr *hints)
{
	if (!hints)
		return;

	if (hints->tx_ctx_cnt)
		attr->tx_ctx_cnt = hints->tx_ctx_cnt;
	if (hints->rx_ctx_cnt)
		attr->rx_ctx_cnt = hints->rx_ctx_cnt;
}

static void fi_alter_rx_attr(struct fi_rx_attr *attr,
			     const struct fi_rx_attr *hints,
			     uint64_t info_caps)
{
	if (!hints) {
		attr->caps = (info_caps & attr->caps & FI_PRIMARY_CAPS) |
			     (attr->caps & FI_SECONDARY_CAPS);
		return;
	}

	attr->op_flags = hints->op_flags;
	attr->caps = (hints->caps & FI_PRIMARY_CAPS) |
		     (attr->caps & FI_SECONDARY_CAPS);
	attr->total_buffered_recv = hints->total_buffered_recv;
	if (hints->size)
		attr->size = hints->size;
	if (hints->iov_limit)
		attr->iov_limit = hints->iov_limit;
}

static void fi_alter_tx_attr(struct fi_tx_attr *attr,
			     const struct fi_tx_attr *hints,
			     uint64_t info_caps)
{
	if (!hints) {
		attr->caps = (info_caps & attr->caps & FI_PRIMARY_CAPS) |
			     (attr->caps & FI_SECONDARY_CAPS);
		return;
	}

	attr->op_flags = hints->op_flags;
	attr->caps = (hints->caps & FI_PRIMARY_CAPS) |
		     (attr->caps & FI_SECONDARY_CAPS);
	if (hints->inject_size)
		attr->inject_size = hints->inject_size;
	if (hints->size)
		attr->size = hints->size;
	if (hints->iov_limit)
		attr->iov_limit = hints->iov_limit;
	if (hints->rma_iov_limit)
		attr->rma_iov_limit = hints->rma_iov_limit;
}

/*
 * Alter the returned fi_info based on the user hints.  We assume that
 * the hints have been validated and the starting fi_info is properly
 * configured by the provider.
 */
void fi_alter_info(struct fi_info *info,
		   const struct fi_info *hints)
{
	if (!hints)
		return;

	info->caps = (hints->caps & FI_PRIMARY_CAPS) |
		     (info->caps & FI_SECONDARY_CAPS);

	fi_alter_ep_attr(info->ep_attr, hints->ep_attr);
	fi_alter_rx_attr(info->rx_attr, hints->rx_attr, info->caps);
	fi_alter_tx_attr(info->tx_attr, hints->tx_attr, info->caps);
}
