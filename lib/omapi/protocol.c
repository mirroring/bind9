/*
 * Copyright (C) 1996, 1997, 1998, 1999  Internet Software Consortium.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/*
 * Functions supporting the object management protocol.
 */
#include <stddef.h>		/* NULL */
#include <stdlib.h>		/* random */
#include <string.h>		/* memset */

#include <isc/assertions.h>
#include <isc/error.h>

#include <omapi/private.h>

typedef enum {
	omapi_protocol_intro_wait,
	omapi_protocol_header_wait,
	omapi_protocol_signature_wait,
	omapi_protocol_name_wait,
	omapi_protocol_name_length_wait,
	omapi_protocol_value_wait,
	omapi_protocol_value_length_wait
} omapi_protocol_state_t;

typedef struct {
	OMAPI_OBJECT_PREAMBLE;
} omapi_protocol_listener_object_t;

typedef struct {
	OMAPI_OBJECT_PREAMBLE;
	unsigned int			header_size;		
	unsigned int			protocol_version;
	isc_uint32_t			next_xid;
	omapi_object_t *		authinfo; /* Default authinfo. */

	omapi_protocol_state_t		state;	/* Input state. */
	/* XXXDCL make isc_boolean_t */
	/*
	 * True when reading message-specific values.
	 */
	isc_boolean_t			reading_message_values;
	omapi_message_object_t *	message;	/* Incoming message. */
	omapi_data_string_t *		name;		/* Incoming name. */
	omapi_typed_data_t *		value;		/* Incoming value. */
} omapi_protocol_object_t;

/*
 * OMAPI protocol header, version 1.00
 */
typedef struct {
	unsigned int authlen;  /* Length of authenticator. */
	unsigned int authid;   /* Authenticator object ID. */
	unsigned int op;       /* Opcode. */
	omapi_handle_t handle; /* Handle of object being operated on, or 0. */
	unsigned int id;	/* Transaction ID. */
	unsigned int rid;       /* ID of transaction responding to. */
} omapi_protocol_header_t;

isc_result_t
omapi_protocol_connect(omapi_object_t *h, const char *server_name,
		       int port, omapi_object_t *authinfo)
{
	isc_result_t result;
	omapi_protocol_object_t *obj;

	obj = NULL;
	result = omapi_object_create((omapi_object_t **)&obj,
				     omapi_type_protocol, sizeof(*obj));
	if (result != ISC_R_SUCCESS)
		return (result);

	result = omapi_connection_toserver((omapi_object_t *)obj,
					   server_name, port);
	if (result != ISC_R_SUCCESS) {
		OBJECT_DEREF(&obj);
		return (result);
	}
	OBJECT_REF(&h->outer, obj);
	OBJECT_REF(&obj->inner, h);

	/*
	 * Send the introductory message.
	 */
	result = omapi_protocol_send_intro((omapi_object_t *)obj,
					   OMAPI_PROTOCOL_VERSION,
					   sizeof(omapi_protocol_header_t));
	if (result != ISC_R_SUCCESS) {
		OBJECT_DEREF(&obj);
		return (result);
	}

	if (authinfo)
		OBJECT_REF(&obj->authinfo, authinfo);
	OBJECT_DEREF(&obj);
	return (ISC_R_SUCCESS);
}

void
omapi_protocol_disconnect(omapi_object_t *handle, isc_boolean_t force) {
	omapi_protocol_object_t *protocol;
	omapi_connection_object_t *connection;

	REQUIRE(handle != NULL);

	protocol = (omapi_protocol_object_t *)handle->outer;

	ENSURE(protocol != NULL && protocol->type == omapi_type_protocol);

	connection = (omapi_connection_object_t *)protocol->outer;

	ENSURE(connection != NULL &&
	       connection->type == omapi_type_connection);

	omapi_connection_disconnect((omapi_object_t *)connection, force);
}

/*
 * Send the protocol introduction message.
 */
isc_result_t
omapi_protocol_send_intro(omapi_object_t *h, unsigned int ver,
			  unsigned int hsize)
{
	isc_result_t result;
	omapi_protocol_object_t *p;
	omapi_connection_object_t *connection;

	REQUIRE(h != NULL && h->type == omapi_type_protocol);

	p = (omapi_protocol_object_t *)h;
	connection = (omapi_connection_object_t *)h->outer;

	if (h->outer == NULL || h->outer->type != omapi_type_connection)
		return (OMAPI_R_NOTCONNECTED);

	result = omapi_connection_putuint32((omapi_object_t *)connection,
					     ver);
	if (result != ISC_R_SUCCESS)
		return (result);

	result = omapi_connection_putuint32((omapi_object_t *)connection,
					     hsize);

	if (result != ISC_R_SUCCESS)
		return (result);

	/*
	 * Require the other end to send an intro - this kicks off the
	 * protocol input state machine.
	 */
	p->state = omapi_protocol_intro_wait;
	result = omapi_connection_require((omapi_object_t *)connection, 8);
	if (result != ISC_R_SUCCESS && result != OMAPI_R_NOTYET)
		return (result);

	/*
	 * Make up an initial transaction ID for this connection.
	 * XXXDCL better generator than random()?
	 */
	p->next_xid = random();

	connection_send(connection);

	return (ISC_R_SUCCESS);
}

isc_result_t
omapi_protocol_send_message(omapi_object_t *po, omapi_object_t *id,
			    omapi_object_t *mo, omapi_object_t *omo)
{
	omapi_protocol_object_t *p;
	omapi_object_t *c;
	omapi_message_object_t *m;
	omapi_message_object_t *om;
	omapi_connection_object_t *connection;
	isc_result_t result;

	REQUIRE(po != NULL && po->type == omapi_type_protocol &&
		po->outer != NULL && po->outer->type == omapi_type_connection);
	REQUIRE(mo != NULL && mo->type == omapi_type_message);
	REQUIRE(omo == NULL || omo->type == omapi_type_message);

	p = (omapi_protocol_object_t *)po;
	c = (omapi_object_t *)(po->outer);
	connection = (omapi_connection_object_t *)c;
	m = (omapi_message_object_t *)mo;
	om = (omapi_message_object_t *)omo;

	/* XXXTL Write the authenticator length */
	result = omapi_connection_putuint32(c, 0);
	if (result != ISC_R_SUCCESS)
		return (result);

	/* XXXTL Write the ID of the authentication key we're using. */
	result = omapi_connection_putuint32(c, 0);
	if (result != ISC_R_SUCCESS)
		goto disconnect;

	/*
	 * Write the opcode.
	 */
	result = omapi_connection_putuint32(c, m->op);
	if (result != ISC_R_SUCCESS)
		goto disconnect;

	/*
	 * Write the handle.  If we've been given an explicit handle, use
	 * that.   Otherwise, use the handle of the object we're sending.
	 * The caller is responsible for arranging for one of these handles
	 * to be set (or not).
	 */
	result = omapi_connection_putuint32(c, (m->h ? m->h
						 : (m->object ?
						    m->object->handle
						    : 0)));
	if (result != ISC_R_SUCCESS)
		goto disconnect;

	/*
	 * Set and write the transaction ID.
	 */
	m->id = p->next_xid++;
	result = omapi_connection_putuint32(c, m->id);
	if (result != ISC_R_SUCCESS)
		goto disconnect;

	/*
	 * Write the transaction ID of the message to which this is a
	 * response, if there is such a message.
	 */
	result = omapi_connection_putuint32(c, om ? om->id : m->rid);
	if (result != ISC_R_SUCCESS)
		goto disconnect;

	/*
	 * Stuff out the name/value pairs specific to this message.
	 */
	result = omapi_stuff_values(c, id, (omapi_object_t *)m);
	if (result != ISC_R_SUCCESS)
		goto disconnect;

	/*
	 * Write the zero-length name that terminates the list of name/value
	 * pairs specific to the message.
	 */
	result = omapi_connection_putuint16(c, 0);
	if (result != ISC_R_SUCCESS)
		goto disconnect;

	/*
	 * Stuff out all the published name/value pairs in the object that's
	 * being sent in the message, if there is one.
	 */
	if (m->object != NULL) {
		result = omapi_stuff_values(c, id, m->object);
		if (result != ISC_R_SUCCESS)
			goto disconnect;
	}

	/*
	 * Write the zero-length name that terminates the list of name/value
	 * pairs for the associated object.
	 */
	result = omapi_connection_putuint16(c, 0);
	if (result != ISC_R_SUCCESS)
		goto disconnect;

	/* XXXTL Write the authenticator... */


	/*
	 * When the client sends a message, it expects a reply.  Increment
	 * the count of messages_expected and make sure an isc_socket_recv
	 * gets queued.
	 *
	 * If the connection is in the disconnecting state, connection_send
	 * will note it, with an abort :-), in just a moment.  In any event, it
	 * is decreed to be a fatal error for the client program to call this
	 * function after having asked to disconnect, so going ahead with the
	 * omapi_connection_require call here in the driving thread (rather
	 * than in the task thread, where omapi_protocol_signal_handler
	 * normally does things) is ok.  It is also known that if this is the
	 * only message being sent right now, then there should be no other
	 * recv_done() results coming in until after the
	 * omapi_connection_require(), so some error is not going to be blowing
	 * away the connection.
	 */
	if (connection->is_client) {
		RUNTIME_CHECK(isc_mutex_lock(&connection->mutex) ==
			      ISC_R_SUCCESS);

		if (++connection->messages_expected == 1) {
			/*
			 * omapi_connection_require() needs an unlocked mutex.
			 */
			RUNTIME_CHECK(isc_mutex_unlock(&connection->mutex) ==
				      ISC_R_SUCCESS);
			result = omapi_connection_require(c, p->header_size);

			/*
			 * How could there possibly be that amount of bytes
			 * waiting if no other messages were outstanding?
			 * Answer: it shouldn't be possible.  Make sure.
			 */
			ENSURE(result != ISC_R_SUCCESS);
			if (result != OMAPI_R_NOTYET)
				goto disconnect;

		} else
			/*
			 * If messages_expected > 1, then the code after the
			 * call to omapi_message_process() in the
			 * omapi_protocol_signal_handler function has not yet
			 * been done, so it will handle the call to
			 * omapi_connection_require while messages_expected 
			 * remains non-zero.  (This check also happens at
			 * the end of the block that processes the intro
			 * message.)
			 */
			RUNTIME_CHECK(isc_mutex_unlock(&connection->mutex) ==
				      ISC_R_SUCCESS);
	}

	connection_send(connection);

	return (ISC_R_SUCCESS);

disconnect:
	omapi_connection_disconnect(c, OMAPI_FORCE_DISCONNECT);
	return (result);
}
					  
isc_result_t
omapi_protocol_signal_handler(omapi_object_t *h, const char *name, va_list ap)
{
	isc_result_t result;
	omapi_protocol_object_t *p;
	omapi_object_t *connection;
	omapi_connection_object_t *c;
	isc_uint16_t nlen;
	isc_uint32_t vlen;

	REQUIRE(h != NULL && h->type == omapi_type_protocol);

	p = (omapi_protocol_object_t *)h;
	c = (omapi_connection_object_t *)p->outer;

	/*
	 * Not a signal we recognize?
	 */
	if (strcmp(name, "ready") != 0)
		PASS_SIGNAL(h);

	INSIST(p->outer != NULL && p->outer->type == omapi_type_connection);

	connection = p->outer;

	/*
	 * XXXDCL figure out how come when this function throws
	 * an error, it is not seen by the main program.
	 */

	/*
	 * We get here because we requested that we be woken up after
	 * some number of bytes were read, and that number of bytes
	 * has in fact been read.
	 */
	switch (p->state) {
	case omapi_protocol_intro_wait:
		/*
		 * Get protocol version and header size in network
		 * byte order.
		 */
		omapi_connection_getuint32(connection,
					    (isc_uint32_t *)
					    &p->protocol_version);
		omapi_connection_getuint32(connection,
					    (isc_uint32_t *)&p->header_size);
	
		/*
		 * We currently only support the current protocol version.
		 */
		if (p->protocol_version != OMAPI_PROTOCOL_VERSION) {
			result = OMAPI_R_VERSIONMISMATCH;
			goto disconnect;
		}

		if (p->header_size < sizeof(omapi_protocol_header_t)) {
			result = OMAPI_R_PROTOCOLERROR;
			goto disconnect;
		}

		/*
		 * The next thing that shows up on incoming connections
		 * should be a message header.
		 */
		p->state = omapi_protocol_header_wait;

		/*
		 * Signal omapi_connection_wait() to wake up.
		 * Only do this for the client side.
		 * XXXDCL duplicated below
		 */
		if (c->is_client) {
			RUNTIME_CHECK(isc_mutex_lock(&c->mutex) ==
				      ISC_R_SUCCESS);

			ENSURE(c->messages_expected > 0);
			c->messages_expected--;

			RUNTIME_CHECK(isc_condition_signal(&c->waiter) ==
				      ISC_R_SUCCESS);

			/*
			 * If the driving program has already called
			 * omapi_protocol_send_message and the lock
			 * was acquired in that function, then since
			 * messages_expected would have been >= 2 at
			 * the critical test, the omapi_connection_require
			 * would not have been done yet, and will need
			 * to be.  Since messages_expected was decremented,
			 * drop through to the connection_require only if
			 * messages_expected is >= 1
			 */
			if (c->messages_expected == 0) {
				RUNTIME_CHECK(isc_mutex_unlock(&c->mutex) ==
					      ISC_R_SUCCESS);
				break;
			}

			/*
			 * Proceed to the omapi_connection_require
			 * for the first "real" message's header.
			 */
			RUNTIME_CHECK(isc_mutex_unlock(&c->mutex) ==
				      ISC_R_SUCCESS);
		}

	to_header_wait:

		/*
		 * Register a need for the number of bytes in a header, and if
		 * that many are here already, process them immediately.
		 *
		 * XXXDCL there is a miniscule but non-zero chance that
		 * omapi_connection_require will return ISC_R_NOMEMORY
		 * from omapi_connection_require.  If that happens,
		 * as things are currently written the client will likely
		 * just hang.  no recv was queued, so no recv_done will get
		 * called, so this signal handler never gets called again.
		 */
		result = omapi_connection_require(connection, p->header_size);
		if (result == OMAPI_R_NOTYET)
			break;
		else if (result != ISC_R_SUCCESS)
			goto disconnect;

		/* FALLTHROUGH */

	case omapi_protocol_header_wait:
		result = omapi_message_new((omapi_object_t **)&p->message);
		if (result != ISC_R_SUCCESS)
			goto disconnect;

		/*
		 * Swap in the header.
		 */
		omapi_connection_getuint32(connection,
					  (isc_uint32_t *)&p->message->authid);

		/* XXXTL bind the authenticator here! */
		omapi_connection_getuint32(connection,
					 (isc_uint32_t *)&p->message->authlen);
		omapi_connection_getuint32(connection,
					    (isc_uint32_t *)&p->message->op);
		omapi_connection_getuint32(connection,
					  (isc_uint32_t *)&p->message->handle);
		omapi_connection_getuint32(connection,
					    (isc_uint32_t *)&p->message->id);
		omapi_connection_getuint32(connection,
					    (isc_uint32_t *)&p->message->rid);

		/*
		 * If there was any extra header data, skip over it.
		 */
		if (p->header_size > sizeof(omapi_protocol_header_t))
			omapi_connection_copyout(NULL, connection,
					    (p->header_size -
					     sizeof(omapi_protocol_header_t)));
						     
		/*
		 * XXXTL must compute partial signature across the preceding
		 * bytes.  Also, if authenticator specifies encryption as well
		 * as signing, we may have to decrypt the data on the way in.
		 */

		/*
		 * First we read in message-specific values, then object
		 * values.
		 */
		p->reading_message_values = ISC_TRUE;

	need_name_length:
		/*
		 * The next thing we're expecting is length of the
		 * first name.
		 */
		p->state = omapi_protocol_name_length_wait;

		/*
		 * Wait for a 16-bit length.
		 */
		result = omapi_connection_require(connection, 2);
		if (result == OMAPI_R_NOTYET)
			break;
		else if (result != ISC_R_SUCCESS)
			goto disconnect;

		/* FALLTHROUGH */

	case omapi_protocol_name_length_wait:
		result = omapi_connection_getuint16(connection, &nlen);
		if (result != ISC_R_SUCCESS)
			goto disconnect;

		/*
		 * A zero-length name means that we're done reading name+value
		 * pairs.
		 */
		if (nlen == 0) {
			/*
			 * If we've already read in the object, we are
			 * done reading the message, but if we've just
			 * finished reading in the values associated
			 * with the message, we need to read the
			 * object.
			 */
			if (p->reading_message_values) {
				p->reading_message_values = ISC_FALSE;
				goto need_name_length;
			}

			/*
			 * If the authenticator length is zero, there's no
			 * signature to read in, so go straight to processing
			 * the message.
			 */
			if (p->message->authlen == 0)
				goto message_done;

			/*
			 * The next thing we're expecting is the
			 * message signature.
			 */
			p->state = omapi_protocol_signature_wait;

			/*
			 * Wait for the number of bytes specified for
			 * the authenticator.  If we already have it,
			 * go read it in.
			 */
			result = omapi_connection_require(connection,
						     p->message->authlen);
			if (result == OMAPI_R_NOTYET)
				break;
			else if (result == ISC_R_SUCCESS)
				goto signature_wait;
			else
				goto disconnect;
		}

		/*
		 * Allocate a buffer for the name.
		 */
		result = omapi_data_newstring(&p->name, nlen,
					      "omapi_protocol_signal_handler");
		if (result != ISC_R_SUCCESS)
			goto disconnect;

		p->state = omapi_protocol_name_wait;
		result = omapi_connection_require(connection, nlen);
		if (result == OMAPI_R_NOTYET)
			break;
		else if (result != ISC_R_SUCCESS)
			goto disconnect;

		/* FALLTHROUGH */

	case omapi_protocol_name_wait:
		result = omapi_connection_copyout(p->name->value, connection,
						  p->name->len);
		if (result != ISC_R_SUCCESS)
			goto disconnect;

		/*
		 * Wait for a 32-bit length.
		 */
		p->state = omapi_protocol_value_length_wait;
		result = omapi_connection_require(connection, 4);
		if (result == OMAPI_R_NOTYET)
			break;
		else if (result != ISC_R_SUCCESS)
			goto disconnect;

		/* FALLTHROUGH */

	case omapi_protocol_value_length_wait:
		omapi_connection_getuint32(connection, &vlen);

		/*
		 * Zero-length values are allowed - if we get one, we
		 * don't have to read any data for the value - just
		 * get the next one, if there is a next one.
		 */
		if (vlen == 0)
			goto insert_new_value;

		result = omapi_data_new(&p->value, omapi_datatype_data, vlen,
					"omapi_protocol_signal_handler");
		if (result != ISC_R_SUCCESS)
			goto disconnect;

		p->state = omapi_protocol_value_wait;
		result = omapi_connection_require(connection, vlen);
		if (result == OMAPI_R_NOTYET)
			break;
		else if (result != ISC_R_SUCCESS)
			goto disconnect;
		/*
		 * If it's already here, fall through.
		 */
					     
	case omapi_protocol_value_wait:
		result = omapi_connection_copyout(p->value->u.buffer.value,
						  connection,
						  p->value->u.buffer.len);
		if (result != ISC_R_SUCCESS)
			goto disconnect;

	insert_new_value:
		if (p->reading_message_values) {
			result = omapi_set_value((omapi_object_t *)p->message,
						 p->message->id_object,
						 p->name, p->value);
		} else {
			if (p->message->object == NULL) {
				/*
				 * We need a generic object to hang off of the
				 * incoming message.
				 */
				result =
				       omapi_object_create(&p->message->object,
							   NULL, 0);
				if (result != ISC_R_SUCCESS)
					goto disconnect;
			}

			result = (omapi_set_value
				  ((omapi_object_t *)p->message->object,
				   p->message->id_object,
				   p->name, p->value));
		}
		if (result != ISC_R_SUCCESS)
			goto disconnect;

		omapi_data_stringdereference(&p->name,
					      "omapi_protocol_signal_handler");
		omapi_data_dereference(&p->value);
		goto need_name_length;

	signature_wait:
	case omapi_protocol_signature_wait:
		result = omapi_data_new(&p->message->authenticator,
					omapi_datatype_data,
					p->message->authlen);

		if (result != ISC_R_SUCCESS)
			goto disconnect;

		result = (omapi_connection_copyout
			  (p->message->authenticator->u.buffer.value,
			   connection, p->message->authlen));
		if (result != ISC_R_SUCCESS)
			goto disconnect;

		/* XXXTL now do something to verify the signature. */

		/*
		 * Process the message.
		 */
	message_done:
		result = omapi_message_process((omapi_object_t *)p->message,
					       h);

		/* XXXTL unbind the authenticator. */

		/*
		 * Free the message object.
		 */
		OBJECT_DEREF(&p->message);

		/*
		 * The next thing the protocol reads will be a new message.
		 */
		p->state = omapi_protocol_header_wait;

		/*
		 * Signal omapi_connection_wait() to wake up.
		 * XXXDCL duplicated from above.
		 */
		if (c->is_client) {
			RUNTIME_CHECK(isc_mutex_lock(&c->mutex) ==
				      ISC_R_SUCCESS);

			ENSURE(c->messages_expected > 0);
			c->messages_expected--;

			RUNTIME_CHECK(isc_condition_signal(&c->waiter) ==
				      ISC_R_SUCCESS);

			/*
			 * If there are no more messages expected, exit
			 * the signal handler.
			 */
			if (c->messages_expected == 0) {
				RUNTIME_CHECK(isc_mutex_unlock(&c->mutex) ==
					      ISC_R_SUCCESS);
				break;
			}

			RUNTIME_CHECK(isc_mutex_unlock(&c->mutex) ==
				      ISC_R_SUCCESS);
		}

		/*
		 * Proceed to the omapi_connection_require for the next
		 * message's header.
		 */

		/*
		 * XXXDCL these gotos could be cleared up with one
		 * more variable to control a loop around the switch.
		 */
		fprintf(stderr, "going to header_wait, events_pending = %d"
			" messages_expected = %d\n", c->events_pending,
			c->messages_expected);
		goto to_header_wait;		

	default:
		UNEXPECTED_ERROR(__FILE__, __LINE__, "unknown state in "
				 "omapi_protocol_signal_handler: %d\n",
				 p->state);

		break;
	}

	return (ISC_R_SUCCESS);

	/* XXXDCL
	 * 'goto' could be avoided by wrapping the body in another function.
	 * btw, what happens in C when a file has multiple instances of
	 * the same label?
	 */
disconnect:
	omapi_connection_disconnect(connection, OMAPI_FORCE_DISCONNECT);
	return (result);
}

isc_result_t
omapi_protocol_set_value(omapi_object_t *h, omapi_object_t *id,
			 omapi_data_string_t *name, omapi_typed_data_t *value)
{
	REQUIRE(h != NULL && h->type == omapi_type_protocol);

	PASS_SETVALUE(h);
}

isc_result_t
omapi_protocol_get_value(omapi_object_t *h, omapi_object_t *id,
			 omapi_data_string_t *name,
			 omapi_value_t **value)
{
	REQUIRE(h != NULL && h->type == omapi_type_protocol);
	
	PASS_GETVALUE(h);
}

void
omapi_protocol_destroy(omapi_object_t *h) {
	omapi_protocol_object_t *p;

	REQUIRE(h != NULL && h->type == omapi_type_protocol);

	p = (omapi_protocol_object_t *)h;

	if (p->message != NULL)
		OBJECT_DEREF(&p->message);

	if (p->authinfo != NULL)
		OBJECT_DEREF(&p->authinfo);
}

/*
 * Write all the published values associated with the object through the
 * specified connection.
 */

isc_result_t
omapi_protocol_stuff_values(omapi_object_t *connection, omapi_object_t *id,
			    omapi_object_t *h)
{
	REQUIRE(h != NULL && h->type == omapi_type_protocol);

	PASS_STUFFVALUES(h);
}

/*
 * Set up a listener for the omapi protocol.    The handle stored points to
 * a listener object, not a protocol object.
 */

isc_result_t
omapi_protocol_listen(omapi_object_t *h, int port, int max) {
	isc_result_t result;
	omapi_protocol_listener_object_t *obj;

	obj = isc_mem_get(omapi_mctx, sizeof(*obj));
	if (obj == NULL)
		return (ISC_R_NOMEMORY);
	memset(obj, 0, sizeof(*obj));
	obj->object_size = sizeof(*obj);
	obj->refcnt = 1;
	obj->type = omapi_type_protocol_listener;

	OBJECT_REF(&h->outer, obj);
	OBJECT_REF(&obj->inner, h);

	result = omapi_listener_listen((omapi_object_t *)obj, port, max);

	OBJECT_DEREF(&obj);
	return (result);
}

/*
 * Signal handler for protocol listener - if we get a connect signal,
 * create a new protocol connection, otherwise pass the signal down.
 */

isc_result_t
omapi_protocol_listener_signal(omapi_object_t *h, const char *name, va_list ap)
{
	isc_result_t result;
	omapi_object_t *c;
	omapi_protocol_object_t *obj;
	omapi_protocol_listener_object_t *p;

	REQUIRE(h != NULL && h->type == omapi_type_protocol_listener);

	p = (omapi_protocol_listener_object_t *)h;

	/*
	 * Not a signal we recognize?
	 */
	if (strcmp(name, "connect") != 0) {
		PASS_SIGNAL(h);
	}

	c = va_arg(ap, omapi_object_t *);

	ENSURE(c != NULL && c->type == omapi_type_connection);

	/*
	 * Create a new protocol object to oversee the handling of this
	 * connection.
	 */
	obj = NULL;
	result = omapi_object_create((omapi_object_t **)&obj,
				     omapi_type_protocol, sizeof(*obj));
	if (result != ISC_R_SUCCESS)
		/*
		 * When the unsuccessful return value is percolated back to
		 * omapi_listener_accept, then it will remove the only
		 * reference, which will close and cleanup the connection.
		 */
		return (result);

	/*
	 * Tie the protocol object bidirectionally to the connection
	 * object, with the connection as the outer object.
	 */
	OBJECT_REF(&obj->outer, c);
	OBJECT_REF(&c->inner, obj);

	/*
	 * Send the introductory message.
	 */
	result = omapi_protocol_send_intro((omapi_object_t *)obj,
					   OMAPI_PROTOCOL_VERSION,
					   sizeof(omapi_protocol_header_t));

	if (result != ISC_R_SUCCESS)
		/*
		 * Remove the protocol object's reference to the connection
		 * object, so that when the unsuccessful return value is
		 * received in omapi_listener_accept, the connection object
		 * will be destroyed.
		 * XXXDCL aigh, this is so confusing.  I don't think the
		 * right thing is being done.
		 */
		OBJECT_DEREF(&c->inner);

	/*
	 * Remove one of the references to the object, so it will be
	 * freed when the connection dereferences its inner object.
	 * XXXDCL this is what ted did, but i'm not sure my explanation
	 * is correct.
	 */
	OBJECT_DEREF(&obj);

	return (result);
}

isc_result_t
omapi_protocol_listener_set_value(omapi_object_t *h, omapi_object_t *id,
				  omapi_data_string_t *name,
				  omapi_typed_data_t *value)
{
	REQUIRE(h != NULL && h->type == omapi_type_protocol_listener);

	PASS_SETVALUE(h);
}

isc_result_t
omapi_protocol_listener_get_value(omapi_object_t *h, omapi_object_t *id,
				  omapi_data_string_t *name,
				  omapi_value_t **value)
{
	REQUIRE(h != NULL && h->type == omapi_type_protocol_listener);

	PASS_GETVALUE(h);
}

void
omapi_protocol_listener_destroy(omapi_object_t *h) {
	REQUIRE(h != NULL && h->type == omapi_type_protocol_listener);

	/* XXXDCL currently NOTHING */
}

/*
 * Write all the published values associated with the object through the
 * specified connection.
 */

isc_result_t
omapi_protocol_listener_stuff(omapi_object_t *connection, omapi_object_t *id,
			      omapi_object_t *h)
{

	REQUIRE(h != NULL && h->type == omapi_type_protocol_listener);

	PASS_STUFFVALUES(h);
}

isc_result_t
omapi_protocol_send_status(omapi_object_t *po, omapi_object_t *id,
			   isc_result_t waitstatus,
			   unsigned int rid, const char *msg)
{
	isc_result_t result;
	omapi_object_t *message = NULL;

	REQUIRE(po != NULL && po->type == omapi_type_protocol);

	result = omapi_message_new(&message);
	if (result != ISC_R_SUCCESS)
		return (result);

	result = omapi_set_int_value(message, NULL, "op", OMAPI_OP_STATUS);

	if (result == ISC_R_SUCCESS)
		result = omapi_set_int_value(message, NULL, "rid", (int)rid);

	if (result == ISC_R_SUCCESS)
		result = omapi_set_int_value(message, NULL, "result",
					     (int)waitstatus);

	/*
	 * If a message has been provided, send it.
	 */
	if (result == ISC_R_SUCCESS && msg != NULL)
		result = omapi_set_string_value(message, NULL, "message", msg);

	if (result != ISC_R_SUCCESS) {
		OBJECT_DEREF(&message);
		return (result);
	}

	return (omapi_protocol_send_message(po, id, message, NULL));
}

isc_result_t
omapi_protocol_send_update(omapi_object_t *po, omapi_object_t *id,
			   unsigned int rid, omapi_object_t *object)
{
	isc_result_t result;
	omapi_object_t *message = NULL;

	REQUIRE(po != NULL && po->type == omapi_type_protocol);

	result = omapi_message_new(&message);
	if (result != ISC_R_SUCCESS)
		return (result);

	result = omapi_set_int_value(message, NULL, "op", OMAPI_OP_UPDATE);

	if (result == ISC_R_SUCCESS && rid != 0) {
		omapi_handle_t handle;

		result = omapi_set_int_value(message, NULL, "rid", (int)rid);

		if (result == ISC_R_SUCCESS)
			result = omapi_object_handle(&handle, object);

		if (result == ISC_R_SUCCESS)
			result = omapi_set_int_value(message, NULL,
						     "handle", (int)handle);
	}		
		
	if (result == ISC_R_SUCCESS)
		result = omapi_set_object_value(message, NULL,
						"object", object);

	if (result != ISC_R_SUCCESS) {
		OBJECT_DEREF(&message);
		return (result);
	}

	return (omapi_protocol_send_message(po, id, message, NULL));
}
